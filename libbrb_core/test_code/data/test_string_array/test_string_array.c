/*
 * test_string_array.c
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

#include <libbrb_core.h>
#include <libbrb_http.h>

/**************************************************************************************************************************/
static char *StringArraySearchVarIntoStrArr2(StringArray *data_strarr, char *target_key_str)
{
	char key_buf[1024];
	char *cur_key_str;
	char *cur_value_offptr;
	int cur_key_sz;
	int target_key_sz;
	int i;

	/* Generate key with terminator */
	target_key_sz = snprintf((char*)&key_buf, sizeof(key_buf), "%s:", target_key_str);

	STRINGARRAY_FOREACH(data_strarr, cur_key_str, cur_key_sz)
	{
		for (i = 0; (i < cur_key_sz); i++)
			if  (cur_key_str[i] == ':')
				break;

		if ((i + 1) != target_key_sz)
			continue;

		/* Keys match, calculate value offset */
		if (!memcmp(cur_key_str, &key_buf, target_key_sz))
		{
			cur_value_offptr = (cur_key_str + i + 2);

//			KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_GREEN, "PARAM [%s] - [%s]\n",
//					cur_value_offptr, cur_key_str + i + 1);

			return cur_value_offptr;
		}
	}

	/* Not found */
	return NULL;
}
/**************************************************************************************************************************/
int main(void)
{
	StringArray *str_arr;
	char *elem_str;
	int elem_sz;
	int elem_count;

	char *buffer_str = ",,2,,4,,6teste@123,,asd,,,11";
	char *value_str;
	MemBuffer *data_mb = MemBufferNew(BRBDATA_THREAD_UNSAFE, 256);
	int op_status;

//	str_arr 	= StringArrayExplodeStr(buffer_str, ",", "\\", NULL);
//	str_arr 	= StringArrayExplodeStrNSOF(buffer_str, ",", NULL, NULL, -1);
//	str_arr 	= StringArrayExplodeStr(buffer_str, ",", NULL, NULL);
	str_arr 	= StringArrayExplodeStrN(buffer_str, ",", NULL, NULL, strlen(buffer_str));

//	str_arr 	= StringArrayExplodeStr("REPLY-4#0#0\nFINISH#0\r", "\r", NULL, NULL);
//	str_arr 	= StringArrayExplodeLargeStrN("REPLY-4#0#0\nFINISH#0\r", "\r", NULL, NULL, 8092);


	elem_count		= StringArrayGetElemCount(str_arr);

	printf("FOUND %d in %s\n", elem_count, buffer_str);

	STRINGARRAY_FOREACH(str_arr, elem_str, elem_sz)
	{
		printf("%d %s\n", _count_, elem_str);
	}

	//	str_arr 	= StringArrayExplodeStr(buffer_str, "\n", NULL, NULL);
	//	value_str	= StringArraySearchVarIntoStrArr2(str_arr, "DTMF-Digit");
//	printf("EVENT [%s]", value_str);

	StringArrayDebugShow(str_arr, stdout);

	return 1;
}
/**************************************************************************************************************************/
//int test_stringarray(void)
//{
//
//	StringArray *str_arr;
//	char *str_ptr;
//	int i;
//
//	for (i = 0; i < 65535; i++)
//	{
//		//str_arr = StringArrayExplodeStr("aaaa#eeeee#iiiii#ooooo#uuuuuu", "#");
//
//		StringArrayPrint(str_arr, stdout);
//
//		StringArrayDestroy(str_arr);
//	}
//
//}
/**************************************************************************************************************************/
