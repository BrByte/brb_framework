/*
 * test_assoc_array.c
 *
 *  Created on: 2011-10-11
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

#include <libbrb_data.h>

#define KV_MASK "%s,%s"
#define HASH_TABLE_SIZE 1024 * 1024

typedef struct _Sample {
	HashTableV2Item hash_item;
	char key[256];
	char value[256];

	BRB_MD5_CTX md5_key;
} Sample;

HashTableV2 *glob_hash_table;

static int HashPopulate(void);
static int HashSearch(void);


static void SampleDestroy(void *ptr);
static void SampleShow(Sample *sample_item);
static Sample *SampleNew(char *key, char *value);

/**************************************************************************************************************************/
static int HashPopulate(void)
{
	Sample *sample_item;
	char key[256];
	char value[256];
	unsigned int value_int;
	int i;

	printf ("Generating hash table with [%u] items\n", HASH_TABLE_SIZE);

	for (i = 0; i < HASH_TABLE_SIZE; i++)
	{
		value_int = (unsigned int)arc4random() % 10241024;

		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));

		sprintf((char*)&key, "teste_key-%08d", value_int);
		sprintf((char*)&value, "teste_value-%08d", value_int);

		/* Create a new sample item */
		sample_item 		= SampleNew((char*)key, (char*)value);

		/* Clean up stack */
		memset(&sample_item->md5_key, 0, sizeof(BRB_MD5_CTX));

		/* Generate a HASH signature of TABLE NAME */
		BRB_MD5Init(&sample_item->md5_key);
		BRB_MD5Update(&sample_item->md5_key, (char*)&key, sizeof(key));
		BRB_MD5Final(&sample_item->md5_key);

		/* Store it into HASH */
		HashTableV2ItemAdd(glob_hash_table, &sample_item->hash_item, sample_item, (char*)&sample_item->md5_key, sizeof(sample_item->md5_key));
		continue;
	}

	return 1;
}
/**************************************************************************************************************************/
static int HashSearch(void)
{
	Sample *sample_item;
	Sample *sample_item_ptr;
	struct timeval current_time;
	HashTableV2Item *hash_item;
	long int begin_cur_time;
	long int begin_cur_microtime;
	long int finish_cur_time;
	long int finish_cur_microtime;

	BRB_MD5_CTX md5_key;
	char key[256];
	unsigned int value_int;
	int i;

	printf ("Searching on hash table..\n");

	for (i = 0; i < HASH_TABLE_SIZE; i++)
	{
		//value_int = ((unsigned int)arc4random() % (HASH_TABLE_SIZE - 1));
		value_int = (unsigned int)arc4random() % 10241024;

		memset(&key, 0, sizeof(key));

		sprintf((char*)&key, "teste_key-%08X", value_int);

		/* Benchmark */
		gettimeofday(&current_time, NULL);
		begin_cur_time = current_time.tv_sec;
		begin_cur_microtime =  current_time.tv_usec;

		printf("Looking up key: [%s]...\n ", key);

		/* Clean up stack */
		memset(&md5_key, 0, sizeof(BRB_MD5_CTX));

		/* Generate a HASH signature of TABLE NAME */
		BRB_MD5Init(&md5_key);
		BRB_MD5Update(&md5_key, (char*)&key, sizeof(key));
		BRB_MD5Final(&md5_key);

		/* Do the lookup */
		hash_item 			= HashTableV2ItemFind(glob_hash_table, (char*)&md5_key.digest, sizeof(md5_key.digest));

		/* Do the lookup */
//		hash_item 			= HashTableV2ItemFind(glob_hash_table, (char*)&key, sizeof(key));
		sample_item_ptr 	= (hash_item ? hash_item->data_ptr : NULL);

		/* Benchmark */
		gettimeofday(&current_time, NULL);
		finish_cur_time = current_time.tv_sec;
		finish_cur_microtime =  current_time.tv_usec;

		if (sample_item_ptr)
		{
			printf("Found! Value is [%p] - Bucket_id [%d] bucket items [%lu] | Took [%ld - %ld]\n",
					sample_item_ptr,  hash_item->bucket->id, hash_item->bucket->list.size, (finish_cur_time - begin_cur_time), (finish_cur_microtime - begin_cur_microtime));

			SampleShow(sample_item_ptr);
			HashTableV2ItemDel(glob_hash_table, hash_item);
			SampleDestroy(sample_item_ptr);
			abort();
		}
		else
		{
			printf("Not found!                             - Took [%ld - %ld]\n", (finish_cur_time - begin_cur_time), (finish_cur_microtime - begin_cur_microtime));
		}

		continue;
	}



	return 1;
}
/**************************************************************************************************************************/
int main(void)
{
	HashTableV2Item *hash_item;
	HashTableConfig hash_cfg;

	struct timeval current_time;
	long int begin_cur_time;
	long int begin_cur_microtime;
	long int finish_cur_time;
	long int finish_cur_microtime;
	Sample *sample_item;
	Sample *sample_item_ptr;
	int i;
	unsigned int value_int;
	char key[256];
	char value[256];

	/* Create new HASH TABLE CONFIGURATION */
	memset(&hash_cfg, 0, sizeof(HashTableConfig));
	hash_cfg.flags.thread_safe = 1;

	/* Create new HASH TABLE */
	glob_hash_table 	= HashTableV2New(&hash_cfg);
//	glob_hash_table 	= HashTableV2New(NULL);

	/* Benchmark */
	gettimeofday(&current_time, NULL);
	begin_cur_time = current_time.tv_sec;
	begin_cur_microtime =  current_time.tv_usec;

	/* Begin populating the hash table */
	HashPopulate();

	/* Benchmark */
	gettimeofday(&current_time, NULL);
	finish_cur_time = current_time.tv_sec;
	finish_cur_microtime =  current_time.tv_usec;

	printf ("Finished generatin hash table - Took [%ld] \n", (finish_cur_time - begin_cur_time) );
	getchar();


	HashSearch();
	getchar();
	HashTableV2Destroy(glob_hash_table);
	getchar();

	//AssocArrayDebugShow(assoc_arr, stdout);

	return 0;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void SampleDestroy(void *ptr)
{
	BRB_FREE(ptr);
}
/**************************************************************************************************************************/
static void SampleShow(Sample *sample_item)
{
	printf("SampleShow - item_key = [%s] - item_value = [%s]\n", sample_item->key, sample_item->value);
}
/**************************************************************************************************************************/
static Sample *SampleNew(char *key, char *value)
{
	Sample *sample_item;

	sample_item = (Sample*)calloc(1,sizeof(Sample));

	strcpy((char*)&sample_item->key, key);
	strcpy((char*)&sample_item->value, value);

	return sample_item;
}
/**************************************************************************************************************************/

