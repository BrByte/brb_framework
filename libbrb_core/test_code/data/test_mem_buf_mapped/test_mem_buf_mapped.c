/*
 * test_mem_buf_mapped.c
 *
 *  Created on: 2014-10-04
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2014 BrByte Software (Oliveira Alves & Amorim LTDA)
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

/************************************************************************************************************************/
int main(int argc, char **argv)
{
	MemBufferMapped *mapped_mb;
	MemBufferMapped *mapped_mb_dump;
	MemBuffer *part_mb;
	DLinkedListNode *node;

	MemBufferMappedValidBytes valid_bytes;
	int i;

	static char *data			= "AAAAAAAA";
	unsigned long data_sz		= strlen(data);
	unsigned long write_offset	= 0;

	mapped_mb = MemBufferMappedNew(BRBDATA_THREAD_UNSAFE, 1024);


//	for (i = 0; i < 100; i++)
//	{
//		MemBufferMappedAdd(mapped_mb, "aaaaaaaaaa", strlen("aaaaaaaaaa"));
//		MemBufferMappedAdd(mapped_mb, "bbbbbbbbbb", strlen("bbbbbbbbbb"));
//		MemBufferMappedAdd(mapped_mb, "cccccccccc", strlen("cccccccccc"));
//		MemBufferMappedAdd(mapped_mb, "dddddddddd", strlen("dddddddddd"));
//	}
//
//	/* Flush last incomplete page */
//	MemBufferMappedCompleted(mapped_mb);
//
//	EvKQBaseLogHexDump(MemBufferDeref(mapped_mb->main_mb), MemBufferGetCapacity(mapped_mb->main_mb) + 1, 8, 4);
//
//	printf("------------------- FINISH WITH PARTIAL_LIST_SZ [%d] - MAIN_SZ [%d] - MAIN_CAP [%d]\n",
//			mapped_mb->partial_mb_list.size,  MemBufferGetSize(mapped_mb->main_mb),  MemBufferGetCapacity(mapped_mb->main_mb));
//
//	for (node = mapped_mb->partial_mb_list.head; node; node = node->next)
//	{
//		part_mb = node->data;
//		MemBufferShow(part_mb, stdout);
//
//		continue;
//	}
//
//
//
//	return 1;

	// 0123|4567|89AB|CDEF|GHIJ|KLMN|OPQR|STUV|XZ

	/* 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 */
	/* 0 0 1 1 1 1 1 1 1 1 0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  */

	data			= "23456789";
	MemBufferMappedWrite(mapped_mb, 2, strlen(data), data);
	write_offset 	+= strlen(data);
	MemBufferMappedGetValidBytes(mapped_mb, 0, 200, &valid_bytes);
	printf("------------------- [%d]- [%ld -> %ld]\n", MemBufferMappedCheckBytes(mapped_mb, 2, 18), valid_bytes.first_byte, valid_bytes.last_byte);


	/* 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 */
	/* 0 0 1 1 1 1 1 1 1 1 0  0  1  1  1  1  1  1  1  1  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  */

	data			= "23456789";
	MemBufferMappedWrite(mapped_mb, 12, strlen(data), data);
	write_offset 	+= strlen(data);
	MemBufferMappedGetValidBytes(mapped_mb, 0, 200, &valid_bytes);
	printf("------------------- [%d]- [%ld -> %ld]\n", MemBufferMappedCheckBytes(mapped_mb, 2, 18), valid_bytes.first_byte, valid_bytes.last_byte);

	/* 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 */
	/* 1 1 1 1 1 1 1 1 1 1 0  0  1  1  1  1  1  1  1  1  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  */

	data			= "01";
	MemBufferMappedWrite(mapped_mb, 0, strlen(data), data);
	write_offset 	+= strlen(data);
	MemBufferMappedGetValidBytes(mapped_mb, 0, 200, &valid_bytes);
	printf("------------------- [%d]- [%ld -> %ld]\n", MemBufferMappedCheckBytes(mapped_mb, 2, 18), valid_bytes.first_byte, valid_bytes.last_byte);

	/* 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 */
	/* 1 1 1 1 1 1 1 1 1 1 1  1  1  1  1  1  1  1  1  1  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  */

	data			= "01";
	MemBufferMappedWrite(mapped_mb, 10, strlen(data), data);
	write_offset 	+= strlen(data);
	MemBufferMappedGetValidBytes(mapped_mb, 0, 200, &valid_bytes);
	printf("------------------- [%d]- [%ld -> %ld]\n", MemBufferMappedCheckBytes(mapped_mb, 2, 18), valid_bytes.first_byte, valid_bytes.last_byte);

	/* 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 */
	/* 1 1 1 1 1 1 1 1 1 1 1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  0  0  0  0  0  0  0  0  0  0  0  */

	data	= "0123456789";
	MemBufferMappedWrite(mapped_mb, write_offset, strlen(data), data);
	MemBufferMappedGetValidBytes(mapped_mb, 0, 200, &valid_bytes);
	printf("write of [%lu]--------------- [%d]- [%ld -> %ld]\n", write_offset, MemBufferMappedCheckBytes(mapped_mb, 2, 18), valid_bytes.first_byte, valid_bytes.last_byte);
	write_offset 	+= strlen(data);

	/* 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 */
	/* 1 1 1 1 1 1 1 1 1 1 1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  0  0  0  0  0  0  0  0  0  0  */

	data	= "A";
	MemBufferMappedWrite(mapped_mb, write_offset, strlen(data), data);
	MemBufferMappedGetValidBytes(mapped_mb, 0, 200, &valid_bytes);
	printf("write of [%lu]--------------- [%d]- [%ld -> %ld]\n", write_offset, MemBufferMappedCheckBytes(mapped_mb, 2, 18), valid_bytes.first_byte, valid_bytes.last_byte);
	write_offset 	+= strlen(data);

	data	= "3456";
	MemBufferMappedWrite(mapped_mb, write_offset, strlen(data), data);
	MemBufferMappedGetValidBytes(mapped_mb, 0, 200, &valid_bytes);
	printf("write of [%lu]--------------- [%d]- [%ld -> %ld]\n", write_offset, MemBufferMappedCheckBytes(mapped_mb, 2, 18), valid_bytes.first_byte, valid_bytes.last_byte);
	write_offset 	+= strlen(data);

	data	= "78";
	MemBufferMappedWrite(mapped_mb, write_offset, strlen(data), data);
	MemBufferMappedGetValidBytes(mapped_mb, 0, 200, &valid_bytes);
	printf("write of [%lu]--------------- [%d]- [%ld -> %ld]\n", write_offset, MemBufferMappedCheckBytes(mapped_mb, 2, 18), valid_bytes.first_byte, valid_bytes.last_byte);
	write_offset 	+= strlen(data);

	data	= "fghijklmnopqrstuvwxyz";
	MemBufferMappedWrite(mapped_mb, write_offset, strlen(data), data);
	MemBufferMappedGetValidBytes(mapped_mb, 0, 200, &valid_bytes);
	printf("write of [%lu]--------------- [%d]- [%ld -> %ld]\n", write_offset, MemBufferMappedCheckBytes(mapped_mb, 2, 18), valid_bytes.first_byte, valid_bytes.last_byte);
	write_offset 	+= strlen(data);

	data	= "12345678901";
	MemBufferMappedWrite(mapped_mb, write_offset, strlen(data), data);
	MemBufferMappedGetValidBytes(mapped_mb, 0, 200, &valid_bytes);
	printf("write of [%lu]--------------- [%d]- [%ld -> %ld]\n", write_offset, MemBufferMappedCheckBytes(mapped_mb, 2, 18), valid_bytes.first_byte, valid_bytes.last_byte);
	write_offset 	+= strlen(data);

	data	= "ZZ012";

	//unsigned long long offx = 4294901760L;
	unsigned long long offx = 65535 * 1024;

	MemBufferMappedWrite(mapped_mb, offx, strlen(data), data);
	MemBufferMappedGetValidBytes(mapped_mb, 0, 200, &valid_bytes);
	printf("write of [%lu]--------------- [%d]- [%ld -> %ld]\n", write_offset, MemBufferMappedCheckBytes(mapped_mb, 2, 18), valid_bytes.first_byte, valid_bytes.last_byte);
	write_offset 	+= strlen(data);

	/* Dump the partial buffer */
	printf("FINISH WITH PARTIAL_LIST_SZ [%lu] - WRITE_OFF [%lu] - MB_SZ [%ld]\n", mapped_mb->partial_mb_list.size, write_offset, MemBufferGetSize(mapped_mb->main_mb));

	for (node = mapped_mb->partial_mb_list.head; node; node = node->next)
	{
		part_mb = node->data;

		printf("PARTIAL BUFFER LEFT WITH IDX [%d]\n", part_mb->user_int);
		MemBufferShow(part_mb, stdout);


		continue;
	}

	//EvKQBaseLogHexDump(MemBufferDeref(mapped_mb->main_mb), MemBufferGetCapacity(mapped_mb->main_mb), 8, 4);

	MemBuffer *packed_mb;
	packed_mb = MemBufferMappedMetaDataPackToMB(mapped_mb, 0);
	MemBufferWriteToFile(packed_mb, "./mapped_mb0.dump");

	/* Begin unpacking back from MB */
	mapped_mb_dump = MemBufferMappedMetaDataUnPackFromMB(packed_mb);

	if (!mapped_mb_dump)
	{
		printf("FAILED - UNPACKING FAILED\n");
		exit(1);
	}

	/* Compare DYN_BITMAPs of LIVE and RECREATED MAPPED_MB */
	if (!DynBitMapCompare(mapped_mb->dyn_bitmap, mapped_mb_dump->dyn_bitmap))
	{
		printf("FAILED - DYN_BITMAPs DIFFER\n");
		abort();
	}
	else
	{
		printf("SUCCESS - DYN_BITMAPs MATCH\n");
	}

	/* Compare DATA SIZE of LIVE and RECREATED MAPPED_MB */
	if (MemBufferGetSize(mapped_mb->main_mb) != MemBufferGetSize(mapped_mb_dump->main_mb))
	{
		printf("FAILED - MAIN_MB SIZE DIFFERS [%lu] - [%lu]\n", MemBufferGetSize(mapped_mb->main_mb), MemBufferGetSize(mapped_mb_dump->main_mb));
		abort();
	}
	else
	{
		printf("SUCCESS - MAIN_MB SIZE MATCH with [%lu] bytes\n", MemBufferGetSize(mapped_mb->main_mb));
	}

	/* Compare DATA of LIVE and RECREATED MAPPED_MB */
	if (memcmp(MemBufferDeref(mapped_mb->main_mb), MemBufferDeref(mapped_mb_dump->main_mb), MemBufferGetSize(mapped_mb_dump->main_mb)))
	{
		printf("FAILED - MAIN_MB DATA DIFFERS [%s] - [%s]\n", MemBufferDeref(mapped_mb->main_mb), MemBufferDeref(mapped_mb_dump->main_mb));
		abort();
	}
	else
	{
		printf("SUCCESS - MAIN_MB DATA MATCH\n");
	}

	packed_mb = MemBufferMappedMetaDataPackToMB(mapped_mb_dump, 0);
	MemBufferWriteToFile(packed_mb, "./mapped_mb1.dump");

	return 1;
}
/************************************************************************************************************************/
