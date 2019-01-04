/*
 * test_dyn_bitmap.c
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
	DynBitMap *dyn_bitmap;
	DynBitMap *dyn_bitmap_dump;
	char string_mask[101];

	long index;
	int i;

	int iter_count		= 128;
	int max_idx_count	= 65535 * 2;

	dyn_bitmap = DynBitMapNew(BRBDATA_THREAD_UNSAFE, 256);

	printf("---------------------------------- [ RANDOM SET - ITER [%d] / MAX_IDX [%d] ] ----------------------------------\n", iter_count, max_idx_count);

	for (i = 0; i < iter_count; i++)
	{
		/* Select a BIT index at random */
		index = (arc4random() % max_idx_count);

		/* Bit already SET, bail out */
		if (DynBitMapBitTest(dyn_bitmap, index))
		{
			//printf("ITER 1 - BIT [%ld] ALREADY SET\n");
			continue;
		}

		//printf("ITER 1 - BIT [%ld] SET\n");

		/* Mark BIT */
		DynBitMapBitSet(dyn_bitmap, index);

		continue;
	}

	printf("---------------------------------- [ START SCAN ] ----------------------------------\n");

	/* Scan for BITs ON */
	for (i = 0; i < max_idx_count; i++)
	{
		/* Bit already SET, bail out */
		if (DynBitMapBitTest(dyn_bitmap, i))
		{
			//printf("ITER 2 - BIT [%ld] SET\n", i);
			continue;
		}
		//else
		//	printf("ITER 2 - BIT [%ld] CLEAR\n", i);

		continue;
	}

	printf("---------------------------------- [ FINISH SCAN ] ----------------------------------\n");
	printf("Finish - BITMAP SIZE IS [%ld] BYTES\n", dyn_bitmap->bitmap.capacity);


	memset(&string_mask, 0, sizeof(string_mask));
	DynBitMapGenerateStringMap(dyn_bitmap, (char*)&string_mask, (sizeof(string_mask) - 1));
	printf("Step 1 - LIVE BITMAP STRING MASK [%s]\n", string_mask);

	/* Begin packing this bitmap */
	MemBuffer *packed_mb;
	packed_mb = DynBitMapMetaDataPackToMB(dyn_bitmap, 0);
	MemBufferWriteToFile(packed_mb, "./dyn_bitmap.dump");

	/* Now re-create the dynamic bitmap back from MEMBUFFER */
	dyn_bitmap_dump = DynBitMapMetaDataUnPackFromMB(packed_mb);

	printf("---------------------------------- [ DUMP START SCAN ] ----------------------------------\n");

	/* Scan for BITs ON */
	for (i = 0; i < max_idx_count; i++)
	{
		/* Bit already SET, bail out */
		if (DynBitMapBitTest(dyn_bitmap_dump, i))
		{
			/* Test versus LIVE BITMAP */
			if (!DynBitMapBitTest(dyn_bitmap_dump, i))
			{
				printf("FAILED - BIT SHIFT ON INDEX [%d]\n", i);
				abort();
			}
			continue;
		}

		continue;
	}

	printf("---------------------------------- [ DUMP FINISH SCAN ] ----------------------------------\n");
	printf("Finish - DUMP BITMAP SIZE IS [%ld] BYTES\n", dyn_bitmap_dump->bitmap.capacity);


	memset(&string_mask, 0, sizeof(string_mask));
	DynBitMapGenerateStringMap(dyn_bitmap_dump, (char*)&string_mask, (sizeof(string_mask) - 1));
	printf("Step 2 - DUMP BITMAP STRING MASK [%s]\n", string_mask);


	return 1;
}
/************************************************************************************************************************/
