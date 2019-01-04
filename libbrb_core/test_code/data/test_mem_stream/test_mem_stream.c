/*
 * test_mem_stream.c
 *
 *  Created on: 2012-11-18
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
	static char *source_a = "aaaaaaaaaaaaaaa";
	int source_a_sz = strlen(source_a);

	static char *source_b = "bbbbbbbbbbb";
	int source_b_sz = strlen(source_b);

	static char *source_c = "cccccccccccccccccccccccccccccccccccccccccccc";
	int source_c_sz = strlen(source_c);

	MemStream *mem_stream = MemStreamNew(4096, MEMSTREAM_MT_UNSAFE);

	MemStreamWrite(mem_stream, source_a, source_a_sz);
	MemStreamWrite(mem_stream, source_b, source_b_sz);
//	MemStreamWrite(mem_stream, source_c, source_c_sz);

	MemStreamWriteToFILE(mem_stream, stdout);

	MemStreamDestroy(mem_stream);

	return 1;
}
/**************************************************************************************************************************/
