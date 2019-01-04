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
#define HASH_TABLE_SIZE 1000

typedef struct _Sample {

	char key[256];
	char value[256];
} Sample;

/**************************************************************************************************************************/
void SampleDestroy(void *ptr)
{
	BRB_FREE(ptr);
}
/**************************************************************************************************************************/
void SampleShow(Sample *sample_item)
{

	printf("SampleShow - item_key = [%s] - item_value = [%s]\n", sample_item->key, sample_item->value);
}
/**************************************************************************************************************************/
Sample *SampleNew(char *key, char *value)
{
	Sample *sample_item;

	sample_item = (Sample*)calloc(1,sizeof(Sample));

	strcpy((char*)&sample_item->key, key);
	strcpy((char*)&sample_item->value, value);

	return sample_item;
}
/**************************************************************************************************************************/
int main(void)
{

	struct timeval current_time;

	long int begin_cur_time;
	long int begin_cur_microtime;

	long int finish_cur_time;
	long int finish_cur_microtime;


	Sample *sample_item;
	AssocArray *assoc_arr;
	Sample *sample_item_ptr;

	int i;
	unsigned int value_int;

	char key[256];
	char value[256];


	/* Create new assoc array */
	assoc_arr = AssocArrayNew(BRBDATA_THREAD_SAFE, 0, SampleDestroy);

	/* Benchmark */
	gettimeofday(&current_time, NULL);
	begin_cur_time = current_time.tv_sec;
	begin_cur_microtime =  current_time.tv_usec;

	printf ("Generating hash table with [%u] items\n", HASH_TABLE_SIZE);

	for (i = 0; i < HASH_TABLE_SIZE; i++)
	{
		value_int = ((unsigned int)arc4random() % (HASH_TABLE_SIZE - 1));

		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));

		sprintf((char*)&key, "teste_key-%04d", i);
		sprintf((char*)&value, "teste_value-%08u", value_int);

		/* Create a new sample item */
		sample_item = SampleNew((char*)key, (char*)value);

		/* Store it into assoc arr */
		AssocArrayAdd(assoc_arr, (char*)&key, sample_item);
	}


	/* Benchmark */
	gettimeofday(&current_time, NULL);
	finish_cur_time = current_time.tv_sec;
	finish_cur_microtime =  current_time.tv_usec;

	printf ("Finished generatin hash table - Took [%ld] \n", (finish_cur_time - begin_cur_time) );
	printf ("Searching on hash table..\n");

	for (i = 0; i < 200; i++)
	{
		value_int = ((unsigned int)arc4random() % (HASH_TABLE_SIZE - 1));

		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));

		sprintf((char*)&key, "teste_key-%04u", value_int);

		/* Benchmark */
		gettimeofday(&current_time, NULL);
		begin_cur_time = current_time.tv_sec;
		begin_cur_microtime =  current_time.tv_usec;

		printf("Looking up key: [%s]...  ", key);

		/* Do the lookup */
		sample_item_ptr = (Sample*)AssocArrayLookup(assoc_arr,  (char*)&key);

		/* Benchmark */
		gettimeofday(&current_time, NULL);
		finish_cur_time = current_time.tv_sec;
		finish_cur_microtime =  current_time.tv_usec;

		if (sample_item_ptr)
		{
			printf("Found! Value is [%p] - Took [%ld - %ld]\n", sample_item_ptr,  (finish_cur_time - begin_cur_time), (finish_cur_microtime - begin_cur_microtime));

			SampleShow(sample_item_ptr);
			AssocArrayDelete(assoc_arr, (char*)&key);
		}
		else
		{
			printf("Not found!                             - Took [%ld - %ld]\n", (finish_cur_time - begin_cur_time), (finish_cur_microtime - begin_cur_microtime));
		}

	//	sleep(1);

	}

	AssocArrayDestroy(assoc_arr);
	//AssocArrayDebugShow(assoc_arr, stdout);

	return 0;
}
/**************************************************************************************************************************/
