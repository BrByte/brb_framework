/*
 * hash_table_v2.c
 *
 *  Created on: 21 de out de 2019
 *      Author: Z800B-Revo2
 */

#include "../include/libbrb_core.h"


/* Private internal functions */
static void HashTableV2HashDecide(HashTableV2 *hash_table, HashTableConfig *config);
static HashTableV2Item *HashTableV2BucketWalk(HashTableV2 *hash_table, HashTableBucket *bucket_ptr, char *key_ptr, int key_sz);

/* Default private HASH and KEY CMP functions */
static HashTableV2HashFunc HashTableV2HashFuncDefault;
static HashTableV2HashFunc HashTableV2HashFuncMurMur;


static HashTableV2KeyCmpFunc HashTableV2KeyCmpFuncDefault;


/**************************************************************************************************************************/
HashTableV2 *HashTableV2New(HashTableConfig *config)
{
	HashTableV2 *hash_table;

	/* Create a new hash table */
	hash_table	= calloc(1, sizeof(HashTableV2));

	/* Load up hash function */
	HashTableV2HashDecide(hash_table, config);

	/* Decide key compare function */
	if (config && config->func.key_cmp)
		hash_table->func.key_cmp	= config->func.key_cmp;
	else
		hash_table->func.key_cmp	= HashTableV2KeyCmpFuncDefault;

	/* Load up static prime numbers */
	hash_table->prime_arr[0]	= 103;
	hash_table->prime_arr[1]	= 229;
	hash_table->prime_arr[2]	= 467;
	hash_table->prime_arr[3]	= 977;
	hash_table->prime_arr[4]	= 1979;
	hash_table->prime_arr[5]	= 4019;
	hash_table->prime_arr[6]	= 6037;
	hash_table->prime_arr[7]	= 7951;
	hash_table->prime_arr[8]	= 12149;
	hash_table->prime_arr[9]	= 16231;
	hash_table->prime_arr[10]	= 33493;
	hash_table->prime_arr[11]	= 65357;
	hash_table->prime_arr[12]	= 128971;
	hash_table->prime_arr[13]	= 256889;
	hash_table->prime_arr[14]	= 513137;
	hash_table->prime_arr[15]	= 1024021;

	/* Max buckets should be a PRIME */
	hash_table->config.max_buckets		= ( (config && config->max_buckets > 0) ? config->max_buckets : 7951);

	/* Load up flags */
	hash_table->flags.item_btf			= (config ? config->flags.item_btf : 0);
	hash_table->flags.item_check_dup	= (config ? config->flags.item_check_dup : 0);
	hash_table->flags.key_match_sz		= (config ? config->flags.key_match_sz : 0);
	hash_table->flags.thread_safe		= (config ? config->flags.thread_safe : 0);

	/* Finally create the buckets ARENA */
	hash_table->buckets 		= MemArenaNew(128, (sizeof(HashTableBucket) + 1), 16, (hash_table->flags.thread_safe ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE));


	return hash_table;
}
/**************************************************************************************************************************/
int HashTableV2Destroy(HashTableV2 *hash_table)
{
	/* Sanity check */
	if (!hash_table)
		return 0;

	MemArenaDestroy(hash_table->buckets);
	free(hash_table);

	return 1;
}
/**************************************************************************************************************************/
int HashTableV2ItemAdd(HashTableV2 *hash_table, HashTableV2Item *hash_item, void *data_ptr, char *key_ptr, int key_sz)
{
	HashTableV2Item *aux_hash_item;
	HashTableBucket *bucket_ptr;
	unsigned int bucket_id;

	/* Sanity check */
	if ((!hash_table) || (!hash_item))
		return 0;

	/* Calculate the bucket ID and grab it from arena */
	bucket_id	= (hash_table->func.hash(key_ptr, key_sz, 12345, hash_table->config.max_buckets)
			% hash_table->config.max_buckets);

	bucket_ptr	= MemArenaGrabByID(hash_table->buckets, bucket_id);

	/* Bucket was not active, initialize list and mark it */
	if (!bucket_ptr->flags.active)
	{
		memset(bucket_ptr, 0, sizeof(HashTableBucket));
		DLinkedListInit(&bucket_ptr->list, BRBDATA_THREAD_UNSAFE);
		bucket_ptr->id				= bucket_id;
		bucket_ptr->flags.active	= 1;
	}
	/* Bucket was active, check for duplicates */
	else if (hash_table->flags.item_check_dup)
	{
		aux_hash_item =  HashTableV2BucketWalk(hash_table, bucket_ptr, key_ptr, key_sz);

		/* Already exists, bail out */
		if (aux_hash_item)
			return 0;
	}

	/* Add into bucket list */
	DLinkedListAdd(&bucket_ptr->list, &hash_item->node, hash_item);
	hash_item->bucket	= bucket_ptr;
	hash_item->data_ptr = data_ptr;
	hash_item->key.ptr	= key_ptr;
	hash_item->key.sz	= key_sz;

	return 1;
}
/**************************************************************************************************************************/
int HashTableV2ItemDel(HashTableV2 *hash_table, HashTableV2Item *hash_item)
{
	HashTableBucket *bucket = hash_item->bucket;

	/* Sanity check */
	if ((!hash_table) || (!hash_item))
		return 0;

	/* Not on hash */
	if (!hash_item->bucket)
		return 0;

	/* Remove from list */
	DLinkedListDelete(&bucket->list, &hash_item->node);

	/* Reset pointers */
	hash_item->bucket	= NULL;
	hash_item->key.ptr	= NULL;
	hash_item->key.sz	= 0;

	/* No more items on this bucket, release */
	if (!bucket->list.head)
		MemArenaReleaseByID(hash_table->buckets, bucket->id);

	return 1;
}
/**************************************************************************************************************************/
HashTableV2Item *HashTableV2ItemFind(HashTableV2 *hash_table, char *key_ptr, int key_sz)
{
	HashTableV2Item *hash_item;
	HashTableBucket *bucket_ptr;
	unsigned int bucket_id;

	/* Sanity check */
	if ((!hash_table) || (!key_ptr))
		return NULL;

	/* Calculate the bucket ID and grab it from arena */
	bucket_id	= (hash_table->func.hash(key_ptr, key_sz, 12345, hash_table->config.max_buckets)
			% hash_table->config.max_buckets);

	bucket_ptr	= MemArenaFindByID(hash_table->buckets, bucket_id);

	/* This bucket is empty, leave */
	if ((!bucket_ptr) || (!bucket_ptr->list.head))
		return NULL;

	/* Walk this bucket */
	hash_item = HashTableV2BucketWalk(hash_table, bucket_ptr, key_ptr, key_sz);

	return hash_item;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void HashTableV2HashDecide(HashTableV2 *hash_table, HashTableConfig *config)
{
	/* Decide hash function to use */
	if (config)
	{
		/* Numeric HASH FUNCTION code, use it */
		if (config->hashfunc_code > 0)
		{
			switch (config->hashfunc_code)
			{
			case HASHTABLE_HASHFUNC_SIMPLE:
				hash_table->func.hash = HashTableV2HashFuncDefault;
				break;
			case HASHTABLE_HASHFUNC_MURMUR:
				hash_table->func.hash = HashTableV2HashFuncMurMur;
				break;
			default:
				abort();
			}

			return;
		}
		/* Has passed AUX external HASH function */
		else if (config->func.hash)
		{
			hash_table->func.hash = config->func.hash;
			return;
		}
	}

	/* No CONFIG passed, fall to default */
	hash_table->func.hash		= HashTableV2HashFuncDefault;
	return;
}
/**************************************************************************************************************************/
static HashTableV2Item *HashTableV2BucketWalk(HashTableV2 *hash_table, HashTableBucket *bucket_ptr, char *key_ptr, int key_sz)
{
	DLinkedListNode *node;
	HashTableV2Item *hash_item;

	/* This bucket has items, walk them */
	for (node = bucket_ptr->list.head; node; node = node->next)
	{
		hash_item = node->data;

		/* Key size differs, ignore */
		if ((hash_table->flags.key_match_sz) && (hash_item->key.sz != key_sz))
			continue;

		/* Item found */
		if (hash_table->func.key_cmp(hash_item, key_ptr, key_sz))
			return hash_item;

		continue;
	}

	return NULL;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static unsigned int HashTableV2KeyCmpFuncDefault(void *hash_item_ptr, char *key_ptr, int key_sz)
{
	HashTableV2Item *hash_item = hash_item_ptr;

	/* Compare with given key pointer */
	if (!memcmp(hash_item->key.ptr, key_ptr, key_sz))
		return 1;

	return 0;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static uint32_t HashTableV2HashFuncROTL32( uint32_t x, int8_t r ) { return (x << r) | (x >> (32 - r)); }
static uint32_t HashTableV2HashFuncFMIX32( uint32_t h ) {h^=h>>16;h*=0x85ebca6b;h^=h>>13;h*=0xc2b2ae35;h^=h>>16;return h;}
/**************************************************************************************************************************/
static unsigned int HashTableV2HashFuncDefault(char *key_ptr, int key_sz, int key_seed, int max_hash_sz)
{
	unsigned int result = key_seed;
	int i;

	for (i = 0; i < key_sz; i++)
	{
		result ^= HashTableV2HashFuncFMIX32(key_ptr[i]);
		result += HashTableV2HashFuncROTL32(key_ptr[i], ((i % 1 == 0) ? 15 : 13));
		continue;
	}

	return (result % max_hash_sz);
}
/**************************************************************************************************************************/
static unsigned int HashTableV2HashFuncMurMur(char *key_ptr, int key_sz, int key_seed, int max_hash_sz)
{
	const uint8_t * data	= (const uint8_t*)key_ptr;
	const int nblocks		= (key_sz / 4);
	int seed				= key_seed;
	int i;

	uint32_t h1 = seed;
	uint32_t c1 = 0xcc9e2d51;
	uint32_t c2 = 0x1b873593;

	const uint32_t * blocks = (const uint32_t *)(data + nblocks*4);

	for(i = -nblocks; i; i++)
	{
		uint32_t k1 = blocks[i];

		k1 *= c1;
		k1 = HashTableV2HashFuncROTL32(k1,15);
		k1 *= c2;

		h1 ^= k1;
		h1 = HashTableV2HashFuncROTL32(h1,13);
		h1 = h1*5+0xe6546b64;
	}

	const uint8_t * tail = (const uint8_t*)(data + nblocks*4);

	uint32_t k1 = 0;

	switch(key_sz & 3)
	{
	case 3: k1 ^= tail[2] << 16;
	case 2: k1 ^= tail[1] << 8;
	case 1: k1 ^= tail[0];
	k1 *= c1; k1 = HashTableV2HashFuncROTL32(k1,15);
	k1 *= c2;
	h1 ^= k1;
	};

	h1 ^= key_sz;
	h1 = HashTableV2HashFuncFMIX32(h1);

	return (h1 % max_hash_sz);
}
/**************************************************************************************************************************/
















