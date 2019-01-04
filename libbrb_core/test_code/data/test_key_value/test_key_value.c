/*
 * test_key_value.c
 *
 *  Created on: 2012-01-12
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

#include <libbrb_data.h>
/**************************************************************************************************************************/
int main(void)
{

	static char *conf_path = "kvtest";
	int i = 0;
	char key[64];

	struct timeval current_time;

	long int begin_cur_time;
	long int begin_cur_microtime;
	long int finish_cur_microtime;

	/* Benchmark */
	gettimeofday(&current_time, NULL);
	begin_cur_time = current_time.tv_sec;
	begin_cur_microtime =  current_time.tv_usec;

	fprintf(stdout, "INIT KV...\n");

	/* Add test with new KV */
	KV_ARRAY_DATA *kvdata = KVArrayNew();

	for (i = 0; i <= 100; i++)
	{
		fprintf(stdout, "Add...%d\n", i);

		memset(&key, 0, 63);
		sprintf((char*)&key, "key%d", i);

		KVRowAdd(kvdata, (char*)&key, "value");
	}

	/* Benchmark */
	gettimeofday(&current_time, NULL);
	printf ("Added %d keys - Took [%ld ms] \n", i, (current_time.tv_usec - begin_cur_microtime) );


	fprintf(stdout, "Write to file...\n");
	KVWriteToFile(kvdata, conf_path, 0, 0);

	/* Benchmark */
	gettimeofday(&current_time, NULL);
	printf ("File Written - Took [%ld ms] \n", (current_time.tv_usec - begin_cur_microtime) );

	fprintf(stdout, "Destroy...\n");
	KVDestroy(kvdata);

	/* Benchmark */
	gettimeofday(&current_time, NULL);
	printf ("Destroy - Took [%ld ms] \n", (current_time.tv_usec - begin_cur_microtime) );

	fprintf(stdout, "Reading from file...\n");

	/* Update and Delete test with existent file*/
	kvdata = KVReadFromFile(conf_path, 0);

	/* Benchmark */
	gettimeofday(&current_time, NULL);
	printf ("Read file - Took [%ld ms] \n", (current_time.tv_usec - begin_cur_microtime) );

	fprintf(stdout, "Update...\n");
	KVRowUpdateByKey(kvdata, "key100", "value222");

	/* Benchmark */
	gettimeofday(&current_time, NULL);
	printf ("Update last item - Took [%ld ms] \n", (current_time.tv_usec - begin_cur_microtime) );

	fprintf(stdout, "Delete...\n");
	KVRowDeleteByKey(kvdata, "key99");

	/* Benchmark */
	gettimeofday(&current_time, NULL);
	printf ("Remove last item - Took [%ld ms] \n", (current_time.tv_usec - begin_cur_microtime) );

	fprintf(stdout, "Search key...\n");
	KEY_VALUE *kv_ptr = KVRowGetByKey(kvdata, "key100");

	/* Benchmark */
	gettimeofday(&current_time, NULL);
	printf ("Get last - Took [%ld ms] \n", (current_time.tv_usec - begin_cur_microtime) );

	fprintf(stdout, "Show...\n");
	KVShow(stdout, kv_ptr);

	/* Benchmark */
	gettimeofday(&current_time, NULL);
	printf ("Show - Took [%ld ms] \n", (current_time.tv_usec - begin_cur_microtime) );

	KVWriteToFile(kvdata, conf_path, 0, 0);

	/* Benchmark */
	gettimeofday(&current_time, NULL);
	printf ("Write - Took [%ld ms] \n", (current_time.tv_usec - begin_cur_microtime) );

	KVJsonExport(kvdata);

	/* Benchmark */
	gettimeofday(&current_time, NULL);
	printf ("Write - Took [%ld ms] \n", (current_time.tv_usec - begin_cur_microtime) );

	printf("JsonData: %s\n", MemBufferDeref(kvdata->json_data));

	KVDestroy(kvdata);

}
/**************************************************************************************************************************/
