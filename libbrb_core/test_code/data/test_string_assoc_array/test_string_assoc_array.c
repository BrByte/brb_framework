/*
 * test_string_assoc_array.c
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

#define HASH_TABLE_SIZE 10240

/**************************************************************************************************************************/
int main(void)
{

	struct timeval current_time;

	long int begin_cur_time;
	long int begin_cur_microtime;

	long int finish_cur_time;
	long int finish_cur_microtime;


	StringAssocArray *assoc_arr;
	int i;
	unsigned int value_int;

	char key[256];
	char value[256];


	/* Create new assoc array */
	assoc_arr = StringAssocArrayNew(HASH_TABLE_SIZE);

	/* Benchmark */
	gettimeofday(&current_time, NULL);
	begin_cur_time = current_time.tv_sec;
	begin_cur_microtime =  current_time.tv_usec;


	printf ("Generating hash table with [%u] items\n", HASH_TABLE_SIZE);

	for (i = 0; i < HASH_TABLE_SIZE; i++)
	{
		value_int = ((unsigned int)arc4random()%9999);

		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));

		sprintf((char *)&key, "teste_key-%04d", i);
		sprintf((char *)&value, "teste_value-%08u", value_int);

		StringAssocArrayAdd(assoc_arr, (char*)&key, (char*)&value);
	}


	/* Benchmark */
	gettimeofday(&current_time, NULL);
	finish_cur_time = current_time.tv_sec;
	finish_cur_microtime =  current_time.tv_usec;

	printf ("Finished generatin hash table - Took [%lds] \n", (finish_cur_time - begin_cur_time) );


	printf ("Searching on hash table..\n");
	char *ret_item_str;

	for (i = 0; i < 200; i++)
	{
		value_int = ((unsigned int)arc4random()%9999);

		memset((char *)&key, 0, sizeof(key));
		memset((char *)&value, 0, sizeof(value));

		sprintf((char *)&key, "teste_key-%04u", value_int);

		/* Benchmark */
		gettimeofday(&current_time, NULL);
		begin_cur_time = current_time.tv_sec;
		begin_cur_microtime =  current_time.tv_usec;

		printf("Looking up key: [%s]...  ", key);

		/* Do the lookup */
		ret_item_str = StringAssocArrayLookup(assoc_arr,  (char*)&key);

		/* Benchmark */
		gettimeofday(&current_time, NULL);
		finish_cur_time = current_time.tv_sec;
		finish_cur_microtime =  current_time.tv_usec;

		if (ret_item_str)
		{
			printf("Found! Value is [%s] - Took [%lds - %ldms]\n", ret_item_str,  (finish_cur_time - begin_cur_time), (finish_cur_microtime - begin_cur_microtime));

			StringAssocArrayDelete(assoc_arr, (char*)&key);



		}
		else
		{
			printf("Not found!                             - Took [%lds - %ldms]\n", (finish_cur_time - begin_cur_time), (finish_cur_microtime - begin_cur_microtime));
		}

		//sleep(1);

	}

	//StringAssocArrayDebugShow(assoc_arr, stdout);

	StringAssocArrayDestroy(assoc_arr);

	return 0;
}
/**************************************************************************************************************************/
