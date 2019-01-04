/*
 * test_slotqueue.c
 *
 *  Created on: 2015-01-19
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2015 BrByte Software (Oliveira Alves & Amorim LTDA)
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

#define SLOT_TEST_SIZE 8

static void mainRunTest(void);
static void mainInitArrays(void);
static int MainArrayCommonIsSet(int slot);
static int MainArrayRandomIsSet(int slot);
static unsigned short mainGenerateRandom(void);

SlotQueue slot_queue;
int slot_arr00[SLOT_TEST_SIZE];
int slot_arr01[SLOT_TEST_SIZE];

/************************************************************************************************************************/
int main(void)
{
	int iter_count = 8092;

	/* Clean up stack */
	memset(&slot_queue, 0, sizeof(SlotQueue));

	/* Initialize slot queue and arrays */
	SlotQueueMappedInit(&slot_queue, SLOT_TEST_SIZE, BRBDATA_THREAD_UNSAFE);

	while (iter_count--)
	{
		mainRunTest();
		continue;
	}

	printf("ALL PASS\n");
	return 1;
}
/************************************************************************************************************************/
/**/
/**/
/************************************************************************************************************************/
static void mainRunTest(void)
{
	unsigned short random_slot;
	unsigned short random_busy;
	int slot_id;
	int i, j;


	mainInitArrays();

	/* Generate random busy */
	random_busy = 2; //mainGenerateRandom();
	printf("Will set [%d] slots to BUSY\n", random_busy);

	/* Set random SLOTs as BUSY */
	for (i = 0; i < random_busy; i++)
	{
		random_slot		= mainGenerateRandom();
		slot_arr00[i]	= random_slot;

		SlotQueueMappedSetBusy(&slot_queue, random_slot);
		printf("\t [%d] - Set to busy\n", random_slot);
		continue;
	}

	/* Grab all other possible slots */
	for (i = 0; i < SLOT_TEST_SIZE; i++)
	{
		/* Grab SLOT */
		slot_arr01[i] = SlotQueueGrab(&slot_queue);

		if (slot_arr01[i] < 0)
		{
			printf("\t No more slots, STOP\n");

			if ( (i + random_busy) != SLOT_TEST_SIZE)
			{
				printf("\t RANDOM TEST - ERROR - (BUSY + GRAB) = [%d] - TEST_SIZE = [%d] - CUR_IDX [%d]\n", (i + random_busy), SLOT_TEST_SIZE, slot_queue.slot.index);
				exit(0);
			}
			else
				printf("\t RANDOM TEST - PASS - (BUSY + GRAB) = [%d] - TEST_SIZE = [%d] - CUR_IDX [%d]\n", (i + random_busy), SLOT_TEST_SIZE, slot_queue.slot.index);

			break;
		}

		printf("ITER [%d] - Grabbed slot [%d]\n", i, slot_arr01[i]);
		continue;
	}

	printf("--------------- CURRENT STATE ------------------\n");
	SlotQueueDebugShow(&slot_queue);
	printf("------------------------------------------------\n");

	/* Now check if there is any random number on COMMON */
	for (j = 0; ((j < SLOT_TEST_SIZE) && (slot_arr00[j] > -1)); j++)
	{
		if (MainArrayCommonIsSet(slot_arr00[j]))
		{
			printf("\t RANDOM CHECK ITER [%d] - ERROR - Random slot [%d] also set on common\n", j, slot_arr00[j]);
			exit(0);
		}

	}

	printf("\t RANDOM CHECK - PASS\n");


	/* Now free all slots EXCEPT RANDOM */
	for (j = 0; ((j < SLOT_TEST_SIZE) && (slot_arr01[j] > -1)); j++)
	{
		SlotQueueFree(&slot_queue, slot_arr01[j]);
		printf("ITER [%d] - FREED NON_RANDOM slot [%d]\n", j, slot_arr01[j]);
		continue;
	}

	printf("--------------- CURRENT STATE ------------------\n");
	SlotQueueDebugShow(&slot_queue);
	printf("------------------------------------------------\n");

	/* Check results */
	if ( ((j - i)) != 0)
	{
		printf("\t FREE COMMON - ERROR - (BUSY + GRAB) = [%d] - TEST_SIZE = [%d] - CUR_IDX [%d]\n", ((j - i)), SLOT_TEST_SIZE, slot_queue.slot.index);
		exit(0);
	}
	else
		printf("\t FREE COMMON - PASS - (BUSY + GRAB) = [%d] - TEST_SIZE = [%d] - CUR_IDX [%d]\n", ((j - i)), SLOT_TEST_SIZE, slot_queue.slot.index);


	/* Now free all RANDOM slots */
	for (j = 0; ((j < SLOT_TEST_SIZE) && (slot_arr00[j] > -1)); j++)
	{
		printf("ITER [%d] - FREED random slot [%d]\n", j, slot_arr00[j]);
		SlotQueueFree(&slot_queue, slot_arr00[j]);
		continue;
	}

	/* Check results */
	if ( (j + i) != SLOT_TEST_SIZE)
	{
		printf("\t FREE RANDOM - ERROR - (BUSY + GRAB) = [%d] - TEST_SIZE = [%d] - CUR_IDX [%d]\n", (j + i), SLOT_TEST_SIZE, slot_queue.slot.index);
		exit(0);
	}
	else
		printf("\t FREE RANDOM - PASS - (BUSY + GRAB) = [%d] - TEST_SIZE = [%d] - CUR_IDX [%d]\n", (j + i), SLOT_TEST_SIZE, slot_queue.slot.index);


	printf("--------------- CURRENT STATE ------------------\n");
	SlotQueueDebugShow(&slot_queue);
	printf("------------------------------------------------\n");


	/* Now grab all slots */
	for (i = 0; i < SLOT_TEST_SIZE; i++)
	{
		slot_arr01[i] = SlotQueueGrab(&slot_queue);

		/* No more slots, stop */
		if (slot_arr01[i] < 0)
			break;

		continue;
	}

	/* Check results */
	if (i != SLOT_TEST_SIZE)
	{
		printf("\t LAST GRAB - ERROR - (BUSY + GRAB) = [%d] - TEST_SIZE = [%d] - CUR_IDX [%d]\n", i , SLOT_TEST_SIZE, slot_queue.slot.index);
		exit(0);
	}
	else
		printf("\t LAST GRAB - PASS - (BUSY + GRAB) = [%d] - TEST_SIZE = [%d] - CUR_IDX [%d]\n", i , SLOT_TEST_SIZE, slot_queue.slot.index);

	/* Now free all slots */
	for (i = 0; i < SLOT_TEST_SIZE; i++)
	{
		printf("FREE SLOT [%d]\n", i);
		SlotQueueFree(&slot_queue, i);
		continue;
	}

	printf("----------------------------------------------------------------------\n");

	return;
}
/************************************************************************************************************************/
static void mainInitArrays(void)
{
	int i;
	/* Initialize slots to known value */
	for (i = 0; i < SLOT_TEST_SIZE; i++)
	{
		slot_arr00[i] = -1;
		slot_arr01[i] = -1;
		continue;
	}

	return;
}
/************************************************************************************************************************/
static int MainArrayCommonIsSet(int slot)
{
	int i;

	/* Check if slot is in RANDOM_BUSY_MAP */
	for (i = 0; ((i < SLOT_TEST_SIZE) && (slot_arr01[i] > -1)); i++)
	{
		if (slot_arr01[i] == slot)
			return 1;
	}

	return 0;
}
/************************************************************************************************************************/
static int MainArrayRandomIsSet(int slot)
{
	int i;

	/* Check if slot is in RANDOM_BUSY_MAP */
	for (i = 0; ((i < SLOT_TEST_SIZE) && (slot_arr00[i] > -1)); i++)
	{
		if (slot_arr00[i] == slot)
			return 1;
	}

	return 0;
}
/************************************************************************************************************************/
static unsigned short mainGenerateRandom(void)
{
	unsigned short random_slot;
	int i;

	/* Generate random SLOT */
	while (1)
	{
		random_slot = arc4random();

		/* Size fits */
		if ((random_slot > 0) && (random_slot < SLOT_TEST_SIZE))
		{
			/* Already SET, create a new one */
			if (MainArrayRandomIsSet(random_slot))
				continue;

			/* Satisfied, STOP */
			break;
		}

		continue;
	}

	return random_slot;
}
/************************************************************************************************************************/
