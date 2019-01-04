/*
 * hash_table.c
 *
 *  Created on: 2011-06-13
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
 
/*
 * Hash Tables Implementation
 * *****************************
 *
 * HashTableNew - creates a new hash table, uses the cmp_func  to compare keys.  Returns the identification for the hash table;
 * otherwise returns a negative number on error.
 *
 * HashTableJoinHashItem - joins a HashTableItem under its key lnk->key into the hash table 'hid'.  It does not copy any data
 * into the hash table, only links pointers.
 *
 * HashTableLookup - locates the item under the key 'k' in the hash table 'hid'.
 * Returns a pointer to the hash bucket on success; otherwise returns NULL.
 *
 * HashTableFirstItem - initializes the hash table for the HashTableNextItem() function.
 *
 * HashTableNextItem - returns the next item in the hash table 'hid'.  Otherwise, returns NULL on error or end of list.
 * MUST call HashTableFirstItem() before HashTableNextItem().
 *
 * HashTableLastItem - resets hash traversal state to NULL
 *
 * HashTableRemoveItem - deletes the given HashTableItem node from the hash table 'hid'.  Does not free the item, only removes it from the list.
 * An assertion is triggered if the HashTableItem is not found in the  list.
 *
 * HashTableGetBucket - returns the head item of the bucket in the hash table 'hid'. Otherwise, returns NULL on error.
 *
 * HashTableHashKeyStr - return the key of a HashTableItem as a const string
 *
 */

#include "../include/libbrb_core.h"

#define HASH4a   h = (h << 5) - h + *key++;
#define HASH4b   h = (h << 5) + h + *key++;
#define HASH4 HASH4b

/* Hash primes */
static int hash_primes[] = {103, 229, 467, 977, 1979, 4019,	6037, 7951, 12149, 16231, 33493, 65357};

/**************************************************************************************************************************/
/* Implementation - BEGIN
/**************************************************************************************************************************/
BRBHashTable *HashTableNew(HashCmpFunc * cmp_func, int hash_sz, HashHashFunc * hash_func)
{
	BRBHashTable *hid;

	BRB_CALLOC(hid, 1, sizeof(BRBHashTable));

	if (!hash_sz)
		hid->size = (unsigned int) DEFAULT_HASH_SIZE;
	else
		hid->size = (unsigned int) HashTablePrime(hash_sz);

	/* allocate and null the buckets */
	BRB_CALLOC(hid->buckets, hid->size, sizeof(BRBHashTableItem *));
	hid->cmp			= cmp_func;
	hid->hash			= hash_func;
	hid->next			= NULL;
	hid->current_slot	= 0;

	return hid;
}
/**************************************************************************************************************************/
void HashTableAddItem(BRBHashTable *hid, const char *k, void *item)
{
	unsigned int i;
	BRBHashTableItem *new;

	/* Add to the given hash HashTableItem 'hid' */
	BRB_CALLOC(new, 1, sizeof(BRBHashTableItem));

	/* Populate new item struct */
	new->item	= item;
	new->key	= (char *) k;

	/* Hash it with user-defined hash function */
	i = hid->hash(k, hid->size);

	/* Insert it into hash table */
	new->next		= hid->buckets[i];
	hid->buckets[i] = new;

	hid->count++;

	return;
}
/**************************************************************************************************************************/
void HashTableJoinHashItem(BRBHashTable *hid, BRBHashTableItem *lnk)
{
	unsigned int i;

	/* Hash it with user-defined hash function */
	i = hid->hash(lnk->key, hid->size);

	/* Insert it into hash table */
	lnk->next = hid->buckets[i];
	hid->buckets[i] = lnk;
	hid->count++;

	return;
}
/**************************************************************************************************************************/
BRBHashTableItem *HashTableLookup(BRBHashTable * hid, const void *k)
{
	BRBHashTableItem *walker;
	int b;

	//	assert(k != NULL);

	b = hid->hash(k, hid->size);

	for (walker = hid->buckets[b]; walker != NULL; walker = walker->next)
	{
		/* strcmp of NULL is a SEGV */
		if (NULL == walker->key)
			return NULL;

		if ((hid->cmp) (k, walker->key) == 0)
			return (walker);

		//assert(walker != walker->next);
	}

	return NULL;
}
/**************************************************************************************************************************/
static void HashTableNextBucket(BRBHashTable * hid)
{
	while (hid->next == NULL && ++hid->current_slot < hid->size)
		hid->next = hid->buckets[hid->current_slot];
}
/**************************************************************************************************************************/
void *HashTableFirstItem(BRBHashTable * hid)
{
	//assert(NULL == hid->next);
	hid->current_slot = 0;
	hid->next = hid->buckets[hid->current_slot];

	if (NULL == hid->next)
		HashTableNextBucket(hid);

	return hid->next;
}
/**************************************************************************************************************************/
void *HashTableNextItem(BRBHashTable * hid)
{
	BRBHashTableItem *this = hid->next;

	if (NULL == this)
		return NULL;

	hid->next = this->next;

	if (NULL == hid->next)
		HashTableNextBucket(hid);

	return this;
}
/**************************************************************************************************************************/
void HashTableLastItem(BRBHashTable * hid)
{
	//	assert(hid != NULL);
	hid->next = NULL;
	hid->current_slot = 0;
}
/**************************************************************************************************************************/
int HashTableRemoveItem(BRBHashTable *hid, BRBHashTableItem *hl, int FreeLink)
{
	BRBHashTableItem **P;
	int i;
	//assert(hl != NULL);

	i = hid->hash(hl->key, hid->size);

	for (P = &hid->buckets[i]; *P; P = &(*P)->next)
	{
		if (*P != hl)
			continue;

		*P = hl->next;

		if (hid->next == hl)
		{
			hid->next = hl->next;
			if (NULL == hid->next)
				HashTableNextBucket(hid);
		}

		if (FreeLink)
		{
			BRB_FREE(hl);
		}

		hid->count--;
		return 1;
	}

	return 0;

}
/**************************************************************************************************************************/
BRBHashTableItem *HashTableGetBucket(BRBHashTable *hid, unsigned int bucket)
{
	if (bucket >= hid->size)
		return NULL;

	return (hid->buckets[bucket]);
}
/**************************************************************************************************************************/
void HashTableFreeItems(BRBHashTable *hid, HashFreeFunc *free_func)
{
	BRBHashTableItem *l;
	BRBHashTableItem **list;
	int i = 0;
	int j;

	BRB_CALLOC(list, hid->count, sizeof(BRBHashTableItem *));
	HashTableFirstItem(hid);

	while ((l = HashTableNextItem(hid)) && i < hid->count)
	{
		*(list + i) = l;
		i++;
	}

	for (j = 0; j < i; j++)
		free_func(*(list + j));

	BRB_FREE(list);
}
/**************************************************************************************************************************/
void HashTableFreeMemory(BRBHashTable *hid)
{
	/* Sanity check */
	if (!hid)
		return;

	BRB_FREE(hid->buckets);
	BRB_FREE(hid);
}
/**************************************************************************************************************************/
int HashTablePrime(int n)
{
	int I = sizeof(hash_primes) / sizeof(int);
	int i;
	int best_prime = hash_primes[0];
	double min = fabs(log((double) n) - log((double) hash_primes[0]));
	double d;

	for (i = 0; i < I; i++)
	{
		d = fabs(log((double) n) - log((double) hash_primes[i]));

		if (d > min)
			continue;
		min = d;
		best_prime = hash_primes[i];
	}

	return best_prime;
}
/**************************************************************************************************************************/
const char *HashTableHashKeyStr(BRBHashTableItem *hl)
{
	return (const char *) hl->key;
}
/**************************************************************************************************************************/
void HashTablePrint(BRBHashTable *hid)
{
	int i,n;
	BRBHashTableItem *l;

	for (i = 0; i < hid->count; i++)
	{
		l = hid->buckets[i];
		n = 0;

		while (l != NULL)
			l = l->next;

		printf("%d: %d", i, n);
	}

	printf("\n");
}
/**************************************************************************************************************************/
/* Implementation - END
/**************************************************************************************************************************/


/**************************************************************************************************************************/
/* Hash Support Functions - BEGIN
/**************************************************************************************************************************/
unsigned int HashTableLongNumberHash(const void *data, unsigned int size)
{
	unsigned int result;
	unsigned long key	= (long)data;
	unsigned long seed	= (key / size);

	/* Calculate a result */
	result = (key ^ (seed * 271));
	return (result % size);
}
/**************************************************************************************************************************/
unsigned int HashTableMurMurHash(const void *data, unsigned int size)
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
	unsigned char *d			= (unsigned char*) data; // 32 bit extract from `key'
	const unsigned int *chunks	= NULL;
	const unsigned char *tail	= NULL; // tail - last 8 bytes
	int i						= 0;
	int l						= sizeof(long); // chunk length

	chunks	= (const unsigned int *) (d + l * 4); // body
	tail	= (const unsigned char *) (d + l * 4); // last 8 byte chunk of `key'

	// for each 4 byte chunk of `key'
	for (i = -l; i != 0; ++i)
	{
		// next 4 byte chunk of `key'
		k = chunks[i];

		// encode next 4 byte chunk of `key'
		k *= c1;
		k = (k << r1) | (k >> (32 - r1));
		k *= c2;

		// append to hash
		h ^= k;
		h = (h << r2) | (h >> (32 - r2));
		h = h * m + n;
	}

	k = 0;

	// remainder
	switch (sizeof(long) & 3)
	{ // `len % 4'
	case 3: k ^= (tail[2] << 16);
	case 2: k ^= (tail[1] << 8);
	case 1:
		k ^= tail[0];
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

	return h;
}



/**************************************************************************************************************************/
unsigned int HashTableStringHash(const void *data, unsigned int size)
{
	const char *s = data;
	unsigned int n = 0;
	unsigned int j = 0;
	unsigned int i = 0;
	while (*s) {
		j++;
		n ^= 271 * (unsigned) *s++;
	}
	i = n ^ (j * 271);
	return i % size;
}
/**************************************************************************************************************************/
unsigned int HashTableHash4(const void *data, unsigned int size)
{
	unsigned long len;
	unsigned long loop;
	unsigned int h;
	const char *key = data;

	h = 0;
	len = strlen(key);
	loop = len >> 3;

	switch (len & (8 - 1))
	{
	case 0:
		break;
	case 7:
		HASH4;
		/* FALLTHROUGH */
	case 6:
		HASH4;
		/* FALLTHROUGH */
	case 5:
		HASH4;
		/* FALLTHROUGH */
	case 4:
		HASH4;
		/* FALLTHROUGH */
	case 3:
		HASH4;
		/* FALLTHROUGH */
	case 2:
		HASH4;
		/* FALLTHROUGH */
	case 1:
		HASH4;
	}

	while (loop--)
	{
		HASH4;
		HASH4;
		HASH4;
		HASH4;
		HASH4;
		HASH4;
		HASH4;
		HASH4;
	}
	return h % size;
}
/**************************************************************************************************************************/
unsigned int HashTableURLHash(const void *data, unsigned int size)
{
	const char *s = data;
	unsigned int i, j, n;
	j = strlen(s);
	for (i = j / 2, n = 0; i < j; i++)
		n ^= 271 * (unsigned) s[i];
	i = n ^ (j * 271);
	return i % size;
}
/**************************************************************************************************************************/













/* HASH TABLE USAGE EXAMPLE */
#ifdef HASH_TABLE_USAGE_EXAMPLE
/*
 *  hash-driver - Run with a big file as stdin to insert each line into the
 *  hash table, then prints the whole hash table, then deletes a random item,
 *  and prints the table again...
 */
int
main(void)
{
	HashTable *hid;
	int i;
	LOCAL_ARRAY(char, buf, BUFSIZ);
	LOCAL_ARRAY(char, todelete, BUFSIZ);
	HashTableItem *walker = NULL;

	todelete[0] = '\0';
	printf("init\n");

	printf("creating hash table\n");
	if ((hid = HashTableNew((HashCmpFunc *) strcmp, 229, HashTableHash4)) < 0) {
		printf("HashTableNew error.\n");
		exit(1);
	}
	printf("done creating hash table: %d\n", hid);

	while (fgets(buf, BUFSIZ, stdin)) {
		buf[strlen(buf) - 1] = '\0';
		printf("Inserting '%s' for item %p to hash table: %d\n",
				buf, buf, hid);
		hash_insert(hid, xstrdup(buf), (void *) 0x12345678);
		if (random() % 17 == 0)
			strcpy(todelete, buf);
	}

	printf("walking hash table...\n");
	for (i = 0, walker = HashTableFirstItem(hid); walker; walker = HashTableNextItem(hid)) {
		printf("item %5d: key: '%s' item: %p\n", i++, walker->key,
				walker->item);
	}
	printf("done walking hash table...\n");

	if (todelete[0]) {
		printf("deleting %s from %d\n", todelete, hid);
		if (hash_delete(hid, todelete))
			printf("hash_delete error\n");
	}
	printf("walking hash table...\n");
	for (i = 0, walker = HashTableFirstItem(hid); walker; walker = HashTableNextItem(hid)) {
		printf("item %5d: key: '%s' item: %p\n", i++, walker->key,
				walker->item);
	}
	printf("done walking hash table...\n");


	printf("driver finished.\n");
	exit(0);
}
#endif
