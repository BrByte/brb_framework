/*
 * test_mem_slot.c
 *
 *  Created on: 2014-03-21
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

#include <libbrb_data.h>

typedef struct _TestData
{
	int slot_id;
	int id;

	int count;

	struct {
		unsigned int in_use:1;
	} flags;

} TestData;

static int testAddItem(MemSlotBase *memslot_base, int slot_id, int count);
static int testListItem(MemSlotBase *memslot_base);
/**************************************************************************************************************************/
int main(void)
{
	MemSlotBase memslot_base 	= {0};

	printf("memslot_base - loaded [%d]\n", memslot_base.flags.loaded);

	/* Initialize slot base */
	MemSlotBaseInit(&memslot_base, sizeof(TestData) + 1, 4096, BRBDATA_THREAD_UNSAFE);

	printf("memslot_base - loaded [%d]\n", memslot_base.flags.loaded);

	testAddItem((MemSlotBase *)&memslot_base, 10, 100);
	testAddItem((MemSlotBase *)&memslot_base, 33, 100);
	testAddItem((MemSlotBase *)&memslot_base, 18, 100);
	testAddItem((MemSlotBase *)&memslot_base, 30, 100);
	testAddItem((MemSlotBase *)&memslot_base, 22, 100);
	testAddItem((MemSlotBase *)&memslot_base, 10, 100);
	testAddItem((MemSlotBase *)&memslot_base, 15, 100);
	testAddItem((MemSlotBase *)&memslot_base, 22, 100);
	testAddItem((MemSlotBase *)&memslot_base, 11, 100);
	testAddItem((MemSlotBase *)&memslot_base, 22, 100);

	/* print items */
	testListItem(&memslot_base);

	return 0;
}
/**************************************************************************************************************************/
static int testAddItem(MemSlotBase *memslot_base, int slot_id, int count)
{
	TestData *test_data;

	/* Create new one */
	test_data 				= MemSlotBaseSlotGrabByID(memslot_base, slot_id);

	if (!test_data)
	{
		printf("NO ITEM %d\n", slot_id);
		return -1;
	}

	test_data->id 			= slot_id;
	test_data->count		+= count;
//	test_data->slot_id		= MemSlotBaseSlotGetID(test_data);
	test_data->flags.in_use	= 1;

	return 0;
}
/**************************************************************************************************************************/
static int testListItem(MemSlotBase *memslot_base)
{
	DLinkedListNode *node;
	TestData *test_data;
	int need_comma = 0;

	printf("ITEMS [%lu]\n", memslot_base->list[0].size);

	for (node = memslot_base->list[0].head; node; node = node->next)
	{
		/* Grab data */
		test_data 		= (TestData *)MemSlotBaseSlotData(node->data);

		/* sanitize check */
		if (!test_data)
			continue;

		printf("ITEM [%d] - count [%d]\n", test_data->id, test_data->count);
	}

	return 0;
}
/**************************************************************************************************************************/
