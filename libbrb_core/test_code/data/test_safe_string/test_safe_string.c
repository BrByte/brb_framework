/*
 * test_safe_string.c
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

/****************************************************************************************************/
int main(void)
{

	SafeString *string;
	SafeString *dup_str;
	int i, z;

	string = SafeStringInitFmt(BRBDATA_THREAD_UNSAFE, "essa eh uma eh string eh %d %s %s", 255, "eh", "eh");
	dup_str = SafeStringReplaceSubStrDup(string, "eh", "xyz", 0);

	printf("ORIG: [%s]\n", MemBufferDeref(string));
	printf("NEW:  [%s]\n", MemBufferDeref(dup_str));

	SafeStringDestroy(string);
	SafeStringDestroy(dup_str);


	for (z = 0; z < 2; z++)
	{
		string = SafeStringInitFmt(BRBDATA_THREAD_UNSAFE, "essa eh uma string %d", 255);

		for (i = 0; i < 1024; i++)
		{
			SafeStringAppend(string, " aaaaaaaaaaaaaaa");

			SafeStringAppend(string, " bbbbbbbbbbbbbb");
			SafeStringAppend(string, " cccccccccccccc");
			SafeStringAppend(string, " ddddddddddddddd");
		}

		SafeStringDestroy(string);
	}

}
/**************************************************************************************************************************/
