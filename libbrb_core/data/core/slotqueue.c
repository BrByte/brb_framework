/*
 * slotqueue.c
 *
 *  Created on: 2013-09-10
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2013 BrByte Software (Oliveira Alves & Amorim LTDA)
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

#include "../include/libbrb_core.h"

/**************************************************************************************************************************/
void SlotQueueInit(SlotQueue *slot_queue, int slot_count, LibDataThreadSafeType type)
{
	int min_slots;
	int i, j;

	/* Create the slot arena and busy map */
	slot_queue->slot.arena			= calloc(slot_count, sizeof(int));

	/* Initialize both count and leave index pointing to end of array */
	slot_queue->slot.count = slot_count;
	slot_queue->slot.index = slot_count;

	/* Initialize slots */
	for (i = slot_count, j = 0; i > 0; i--, j++)
		slot_queue->slot.arena[i-1] = j;

	/* Set as loaded */
	slot_queue->flags.loaded = 1;
	return;
}
/**************************************************************************************************************************/
void SlotQueueMappedInit(SlotQueue *slot_queue, int slot_count, LibDataThreadSafeType type)
{
	int min_slots;
	int i, j;

	/* Calculate minimum BITMAP size */
	min_slots = (((slot_queue->slot.count / 8) > 1) ? (slot_queue->slot.count / 8) : 2);

	/* Create the slot arena and busy map */
	slot_queue->slot.arena			= calloc(slot_count, sizeof(int));
	slot_queue->busy_map.private	= DynBitMapNew(BRBDATA_THREAD_UNSAFE, min_slots);
	slot_queue->busy_map.public		= DynBitMapNew(BRBDATA_THREAD_UNSAFE, min_slots);

	/* Initialize both count and leave index pointing to end of array */
	slot_queue->slot.count = slot_count;
	slot_queue->slot.index = slot_count;

	/* Initialize slots */
	for (i = slot_count, j = 0; i > 0; i--, j++)
		slot_queue->slot.arena[i-1] = j;

	/* Set as loaded */
	slot_queue->flags.loaded = 1;
	slot_queue->flags.mapped = 1;
	return;

}

/**************************************************************************************************************************/
void SlotQueueDestroy(SlotQueue *slot_queue)
{
	if (!slot_queue)
		return;

	/* Destroy integer arena */
	if (slot_queue->slot.arena)
		free(slot_queue->slot.arena);

	/* Destroy dynamic bitmap */
	if (slot_queue->flags.mapped)
	{
		DynBitMapDestroy(slot_queue->busy_map.private);
		DynBitMapDestroy(slot_queue->busy_map.public);
	}

	/* Clean up reference */
	slot_queue->busy_map.private	= NULL;
	slot_queue->busy_map.public		= NULL;
	slot_queue->slot.arena			= NULL;

	/* Set as unloaded */
	slot_queue->flags.loaded = 0;
	slot_queue->flags.mapped = 0;
	return;
}
/**************************************************************************************************************************/
void SlotQueueReset(SlotQueue *slot_queue)
{
	int i, j;

	/* Initialize slots */
	for (i = slot_queue->slot.count, j = 0; i > 0; i--, j++)
		slot_queue->slot.arena[i-1] = j;

	/* Reset bit maps */
	if (slot_queue->flags.mapped)
	{
		DynBitMapBitClearAll(slot_queue->busy_map.private);
		DynBitMapBitClearAll(slot_queue->busy_map.public);
	}

	/* Set INDEX */
	slot_queue->slot.index = slot_queue->slot.count;

	return;

}
/**************************************************************************************************************************/
int SlotQueueMappedSetBusy(SlotQueue *slot_queue, int slot)
{
	/* Sanity check */
	if (!slot_queue)
		return 0;

	/* Not mapped, leave */
	if (!slot_queue->flags.mapped)
		return 0;

	/* Already set on public, bail out */
	if (DynBitMapBitTest(slot_queue->busy_map.public, slot))
		return 0;

	/* Already set on private, bail out */
	if (DynBitMapBitTest(slot_queue->busy_map.private, slot))
		return 0;

	/* Set in busy bitmap as busy */
	DynBitMapBitSet(slot_queue->busy_map.public, slot);
	return 1;
}
/**************************************************************************************************************************/
int SlotQueueMappedSetFree(SlotQueue *slot_queue, int slot)
{
	int i, j;
	int slot_idx = -1;

	/* Sanity check */
	if (!slot_queue)
		return 0;

	/* Not mapped, leave */
	if (!slot_queue->flags.mapped)
		return 0;

	/* Already set, bail out */
	if (!DynBitMapBitTest(slot_queue->busy_map.public, slot))
		return 0;

	/* Clear slot in BUSY_MAP */
	DynBitMapBitClear(slot_queue->busy_map.public, slot);

	/* Search for SLOT_QUEUE position */
	for (i = 0; i <= slot_queue->slot.count; i++)
	{
		//printf ("SlotQueueMappedSetFree - ITER [%d] - COMPARING [%d] with [%d]\n", i, slot, slot_queue->slot.arena[i]);

		/* Found it, restore */
		if ( (slot * (-1)) == slot_queue->slot.arena[i])
		{
			slot_idx = i;
			break;
		}

		continue;
	}

	assert(slot_idx >= 0);

	/* Slot is behind index, just set it to POSITIVE */
	if (slot_idx <= slot_queue->slot.index)
	{
		printf("SlotQueueMappedSetFree - No need to move matrix - SLOT_IDX [%d] - CUR_IDX [%d] - VALUE [%d]\n", slot_idx, slot_queue->slot.index, slot);
		slot_queue->slot.arena[slot_idx]	= slot;
	}
	/* Slot is in front of index, we will have to MOVE MEMORY and THIS WILL BE EXPENSIVE */
	else
	{
		printf("SlotQueueMappedSetFree - Will move matrix - SLOT_IDX [%d] - CUR_IDX [%d] - VALUE [%d]\n", slot_idx, slot_queue->slot.index, slot);

		/* Move matrix to the LEFT */
		for ((i = slot_idx); (i <= slot_queue->slot.index); i++)
		{
			slot_queue->slot.arena[i]	= slot_queue->slot.arena[i + 1];
			continue;
		}

		/* Save it on last index */
		slot_queue->slot.arena[++slot_queue->slot.index] = slot;
	}

	printf("--------------- CURRENT STATE ------------------\n");
	SlotQueueDebugShow(slot_queue);
	printf("------------------------------------------------\n");


	return 1;
}

/**************************************************************************************************************************/
int SlotQueueGrab(SlotQueue *slot_queue)
{
	int slot_count;
	int slot;
	int slot_cur;

	/* Sanity check */
	if (!slot_queue)
		return -1;

	/* No available slot */
	if (slot_queue->slot.index <= 0)
		return -1;

	/* Not mapped, return plain old data */
	if (!slot_queue->flags.mapped)
		return slot_queue->slot.arena[--slot_queue->slot.index];

	/* Copy SLOT_COUNT to local VAR to iterate over it */
	slot_count = slot_queue->slot.count;

	/* Try to walk all slots */
	while (slot_count-- >= 0)
	{
		/* Grab slot and decrement index */
		slot_cur							= --slot_queue->slot.index;
		slot								= slot_queue->slot.arena[slot_cur];

		/* No more free slots */
		if (slot_cur < 0)
		{
			printf("SlotQueueGrab - NO MORE SLOTS no WHILE\n");
			return -1;
		}

		/* Mapped slot queue, take action */
		if (slot_queue->flags.mapped)
		{
			printf("SlotQueueGrab - SLOT_ID [%d] - CUR_IDX [%d] - Will grab\n", slot, slot_cur);

			/* This slot is busy, loop */
			if (DynBitMapBitTest(slot_queue->busy_map.public, slot))
			{
				//printf("SLOT_ID [%d] - Set as busy on PUBLIC MAP, ignore\n", slot);
				slot_queue->slot.arena[slot_cur]	= (slot * (-1));
				continue;
			}

			/* Must be free on private */
			assert(!DynBitMapBitTest(slot_queue->busy_map.private, slot));
			slot_queue->slot.arena[slot_cur]	= 9999;
		}

		break;
	}

	/* Set private bitmap */
	DynBitMapBitSet(slot_queue->busy_map.private, slot);

	return slot;
}
/**************************************************************************************************************************/
void SlotQueueFree(SlotQueue *slot_queue, int slot)
{
	int slot_count;

	/* Sanity check */
	if (!slot_queue)
		return;

	if (!slot_queue->flags.mapped)
	{
		/* Set slot back and increment */
		slot_queue->slot.arena[slot_queue->slot.index] = slot;
		slot_queue->slot.index++;

		return;
	}

	/* If this bitmap has been set as busy in PUBLIC MAP, just leave, because its already on MAP */
	if (DynBitMapBitTest(slot_queue->busy_map.public, slot))
	{
		/* Must be free on private */
		assert(!DynBitMapBitTest(slot_queue->busy_map.private, slot));

		/* Clear from public */
		SlotQueueMappedSetFree(slot_queue, slot);
		return;
	}
	/* This slot is not BUSY */
	{
		/* Copy SLOT_COUNT to local VAR to iterate over it */
		slot_count = slot_queue->slot.count;

		/* Walk to next NON_NEGATIVE slot */
		while (slot_count-- >= 0)
		{
			if (slot_queue->slot.arena[slot_queue->slot.index] < 0)
				slot_queue->slot.index++;
			else
				break;

			continue;
		}

		/* Set slot back and increment */
		slot_queue->slot.arena[slot_queue->slot.index] = slot;
		slot_queue->slot.index++;
	}

	/* Must be SET on private - Clear from private */
	assert(DynBitMapBitTest(slot_queue->busy_map.private, slot));
	DynBitMapBitClear(slot_queue->busy_map.private, slot);

	return;
}
/**************************************************************************************************************************/
void SlotQueueDebugShow(SlotQueue *slot_queue)
{
	int i;

	for (i = 0; i < slot_queue->slot.count; i++)
	{
		printf("SLOT_ID [%d] - VALUE [%d]\n", i, slot_queue->slot.arena[i]);
		continue;
	}

	return;
}
/**************************************************************************************************************************/
