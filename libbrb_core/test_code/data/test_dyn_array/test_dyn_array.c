/*
 * test_dyn_array.c
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
int main (void)
{
	DynArray *dyn_arr;
	int i;
	char str_buf[256];
	char *str_ptr;


	/* Create an MT-Safe array */
	dyn_arr = DynArrayNew(BRBDATA_THREAD_SAFE, 256, NULL);

	for (i = 0; i < 6553; i++)
	{
	//	sprintf((char*)&str_buf, "i = %d", i);

	//	str_ptr = strdup(str_buf);

		str_ptr = (char*)&str_buf;

		DynArrayAdd(dyn_arr, str_ptr);

	}

	for (i = 0; i < 6553; i++)
	{

		str_ptr = DynArrayGetDataByPos(dyn_arr, i);

		if (!str_ptr)
		{
			//printf("no str\n");

		}
		else
		{
			//printf("%s\n", str_ptr);

			//BRB_FREE(str_ptr);
		}

	}

	DynArrayDestroy(dyn_arr);

	sleep(1);
}
/**************************************************************************************************************************/

//	DynArray *dyn_arr;
//	int i;
//	SafeString *elem_ptr;
//
//
//	dyn_arr = DynArrayNew(DYNARRAY_MT_SAFE, 8, (ITEMDESTROY_FUNC*)SafeStringDestroy);
//
//	for (i = 0; i < 16; i++)
//	{
//		SafeString *store_ptr;
//
//		store_ptr = SafeStringInitFmt(STRING_MT_SAFE, "line %d", i);
//
//		DynArrayAdd(dyn_arr, store_ptr);
//	}
//
//	DYNARRAY_FOREACH(dyn_arr, elem_ptr)
//	{
//		printf("%s\n", SafeStringDeref(elem_ptr) );
//	}
//
//	DynArrayDestroy(dyn_arr);
//
//	return 1;
/**************************************************************************************************************************/
