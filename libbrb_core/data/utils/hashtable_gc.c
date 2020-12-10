/*
 * hashtable_gc.c
 *
 *  Created on: 2016-08-25
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2016 BrByte Software (Oliveira Alves & Amorim LTDA)
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


#include "../include/libbrb_core.h"

static HashTableGCItem *HashTableGCItemAddInternal(HashTableGC *hash_table, void *key, void *data, int index);


/**************************************************************************************************************************/
HashTableGC *HashTableGCNew(HashTableGCConfig *hash_config)
{
	HashTableGC *hash_table = calloc(1, sizeof(HashTableGC));

	/* Initialize ARENA and LIST */
	hash_table->arena = MemArenaNew(128, (sizeof(HashTableGCItem)), 8, BRBDATA_THREAD_UNSAFE);
	DLinkedListInit(&hash_table->list, BRBDATA_THREAD_UNSAFE);

	/* Load configuration */
	hash_table->func.hash
	hash_table->modulo	= 8092;
	hash_table->key_sz	= 32;

	return hash_table;
}
/**************************************************************************************************************************/
int HashTableGCDestroy(HashTableGC *hash_table)
{
	HashTableGCItem *hash_item;

	/* Sanity check */
	if (!hash_table)
		return 0;

	/* Walk all hash item list */
	while ((hash_item = DLinkedListPopHead(hash_table->list)))
	{
		/* TODO: Invoke ITEM_DESTROY FUNC */
		continue;
	}

	/* Shutdown the arena */
	MemArenaDestroy(hash_table->arena);
	hash_table->arena = NULL;
	free(hash_table);
	return 1;
}
/**************************************************************************************************************************/
HashTableGCItem *HashTableGCDestroyItemAdd(HashTableGC *hash_table, void *key, void *data)
{
	HashTableGCItem *hash_item;


	hash_table->func.hash();

	return HashTableGCItem;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static HashTableGCItem *HashTableGCItemAddInternal(HashTableGC *hash_table, void *key, void *data, int index)
{
	HashTableGCItem *hash_item;

	/* Grab HASH_ITEM at INDEX */
	hash_item = MemArenaGrabByID(hash_table->arena, index);

	if (hash_item->key)
	{

	}
	else
	{
		/* Create room for KEY and load DATA */
		hash_item->key	= malloc(hash_table->key_sz);
		hash_item->data = data;
		hash_item->next = NULL;

		/* Copy KEY */
		memcpy(hash_item->key, key, hash_table->key_sz);

		/* Add to HASH_TABLE list */
		DLinkedListAdd(&hash_table->list, &hash_item->node, hash_item);
	}



	return HashTableGCItem;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
unsigned int HashTableGC_HashFunc_MurMur(const void *key, unsigned int key_size, unsigned int modulo)
{
	unsigned long key			= (long)data;
	unsigned int c1 			= 0xcc9e2d51;
	unsigned int c2 			= 0x1b873593;
	unsigned int r1 			= 15;
	unsigned int r2 			= 13;
	unsigned int m				= 5;
	unsigned int n				= 0xe6546b64;
	unsigned int h				= (key / size);
	unsigned int k				= 0;
	unsigned char *d			= (unsigned char*) data;
	int i						= 0;
	int l						= sizeof(long);
	const unsigned int *chunks	= (const unsigned int *) (d + l * 4);
	const unsigned char *tail	= (const unsigned char *) (d + l * 4);

	/* For EACH 4 BYTES of KEY */
	for (i = -l; i != 0; ++i)
	{
		/* Next 4 bytes */
		k = chunks[i];

		/* Encode next 4 byte chunk of KEY */
		k *= c1;
		k = (k << r1) | (k >> (32 - r1));
		k *= c2;

		/* Append bakc to hash */
		h ^= k;
		h = (h << r2) | (h >> (32 - r2));
		h = h * m + n;
	}

	k = 0;

	/* Remainder */
	switch (sizeof(long) & 3)
	{
	case 3:	k ^= (tail[2] << 16);
	case 2: k ^= (tail[1] << 8);
	case 1:	k ^= tail[0];
		k *= c1;
		k = (k << r1) | (k >> (32 - r1));
		k *= c2;
		h ^= k;
	}

	h ^= sizeof(long);
	h ^= (h >> 16);
	h *= 0x85ebca6b;
	h ^= (h >> 13);
	h *= 0xc2b2ae35;
	h ^= (h >> 16);

	return (h % size);
}
/**************************************************************************************************************************/

