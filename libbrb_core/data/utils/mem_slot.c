/*
 * mem_slot.c
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

#include "../include/libbrb_core.h"

/**************************************************************************************************************************/
int MemSlotBaseInit(MemSlotBase *memslot_base, long slot_sz, long slot_count, LibDataThreadSafeType type)
{
	int i;

	/* Sanity check */
	if (!memslot_base)
		return 0;

	/* Already loaded, bail out */
	if (memslot_base->flags.loaded)
		return 0;

	/* Initialize PTHREAD info */
	memslot_base->mutex				= (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	memslot_base->thrd_cond.mutex	= (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	memslot_base->thrd_cond.cond	= (pthread_cond_t)PTHREAD_COND_INITIALIZER;
	memslot_base->flags.thread_safe	= ((BRBDATA_THREAD_SAFE == type) ? 1 : 0);

	/* Initialize internal data items */
	SlotQueueInit(&memslot_base->slots, slot_count, BRBDATA_THREAD_UNSAFE);

	/* Initialize internal lists */
	for (i = 0; i < MEMSLOT_DEFAULT_LIST_COUNT; i++)
		DLinkedListInit(&memslot_base->list[i], BRBDATA_THREAD_UNSAFE);

	/* Create memory ARENA and mark as LOADED - FIXED 16 object SLOT */
	memslot_base->arena			= MemArenaNew(slot_count, (sizeof(MemSlotMetaData) + slot_sz + 1), 16, BRBDATA_THREAD_UNSAFE);
	memslot_base->flags.loaded = 1;

	return 1;
}
/**************************************************************************************************************************/
int MemSlotBaseClean(MemSlotBase *memslot_base)
{
	/* Sanity check */
	if (!memslot_base)
		return 0;

	/* Running thread safe, destroy MUTEX */
	if (memslot_base->flags.thread_safe)
	{
		MUTEX_DESTROY(memslot_base->mutex, "MEM_SLOT");
		MUTEX_DESTROY(memslot_base->thrd_cond.mutex, "MEM_SLOT_COND");
		pthread_cond_destroy(&memslot_base->thrd_cond.cond);

		/* Reset PTHREAD info */
		memslot_base->mutex				= (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
		memslot_base->thrd_cond.mutex	= (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
		memslot_base->thrd_cond.cond	= (pthread_cond_t)PTHREAD_COND_INITIALIZER;
	}

	/* Destroy memory ARENA */
	MemArenaDestroy(memslot_base->arena);
	SlotQueueDestroy(&memslot_base->slots);

	memslot_base->arena 		= NULL;
	memslot_base->flags.loaded	= 0;

	return 1;
}
/**************************************************************************************************************************/
int MemSlotBaseLock(MemSlotBase *memslot_base)
{
	/* Sanity check */
	if (!memslot_base)
		return 0;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_LOCK(memslot_base->mutex, "MEM_SLOT");

	return 1;
}
/**************************************************************************************************************************/
int MemSlotBaseUnlock(MemSlotBase *memslot_base)
{
	/* Sanity check */
	if (!memslot_base)
		return 0;

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");

	return 1;
}
/**************************************************************************************************************************/
int MemSlotBaseIsEmptyList(MemSlotBase *memslot_base, int list_id)
{
	int is_empty;

	/* Sanity check */
	if (!memslot_base)
		return 0;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_LOCK(memslot_base->mutex, "MEM_SLOT");

	is_empty = DLINKED_LIST_ISEMPTY(memslot_base->list[list_id]);

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");

	return is_empty;
}
/**************************************************************************************************************************/
int MemSlotBaseIsEmpty(MemSlotBase *memslot_base)
{
	int i;

	/* Sanity check */
	if (!memslot_base)
		return 0;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_LOCK(memslot_base->mutex, "MEM_SLOT");

	/* If any of used lists are NOT empty, return FALSE */
	for (i = 0; i < MEMSLOT_DEFAULT_LIST_COUNT; i++)
		if (!DLINKED_LIST_ISEMPTY(memslot_base->list[i]))
		{
			if (memslot_base->flags.thread_safe)
				MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");
			return 0;
		}

	if (memslot_base->flags.thread_safe)
		MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");
	return 1;
}
/**************************************************************************************************************************/
void *MemSlotBaseSlotPointToHeadAndSwitchListID(MemSlotBase *memslot_base, int cur_list_id, int new_list_id)
{
	MemSlotMetaData *slot_meta;
	char *ret_ptr;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_LOCK(memslot_base->mutex, "MEM_SLOT");

	/* List is empty, bail out */
	if (!memslot_base->list[cur_list_id].head)
	{
		if (memslot_base->flags.thread_safe)
			MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");
		return NULL;
	}

	/* Pop item from linked list and free slot */
	slot_meta = memslot_base->list[cur_list_id].head->data;

	/* Nothing left, bail out */
	if (!slot_meta)
	{
		if (memslot_base->flags.thread_safe)
			MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");

		return NULL;
	}

	/* Switch lists and set to new LIST_ID */
	DLinkedListDelete(&memslot_base->list[slot_meta->cur_list_id], &slot_meta->node);
	DLinkedListAddTail(&memslot_base->list[new_list_id], &slot_meta->node, slot_meta);
	slot_meta->cur_list_id = new_list_id;

	/* Grab address and leave */
	ret_ptr = MemSlotBaseSlotData(slot_meta);

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");

	return ret_ptr;
}
/**************************************************************************************************************************/
void *MemSlotBaseSlotPointToHead(MemSlotBase *memslot_base, int list_id)
{
	MemSlotMetaData *slot_meta;
	char *ret_ptr;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_LOCK(memslot_base->mutex, "MEM_SLOT");

	/* List is empty, bail out */
	if (!memslot_base->list[list_id].head)
	{
		if (memslot_base->flags.thread_safe)
			MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");
		return NULL;
	}

	/* Pop item from linked list and free slot */
	slot_meta = memslot_base->list[list_id].head->data;

	/* Nothing left, bail out */
	if (!slot_meta)
	{
		if (memslot_base->flags.thread_safe)
			MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");

		return NULL;
	}

	/* Grab address and leave */
	ret_ptr = MemSlotBaseSlotData(slot_meta);

	if (memslot_base->flags.thread_safe)
		MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");

	return ret_ptr;
}
/**************************************************************************************************************************/
void *MemSlotBaseSlotPointToTail(MemSlotBase *memslot_base, int list_id)
{
	MemSlotMetaData *slot_meta;
	char *ret_ptr;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_LOCK(memslot_base->mutex, "MEM_SLOT");

	/* List is empty, bail out */
	if (!memslot_base->list[list_id].tail)
	{
		if (memslot_base->flags.thread_safe)
			MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");
		return NULL;
	}

	/* Pop item from linked list and free slot */
	slot_meta = memslot_base->list[list_id].tail->data;

	/* Nothing left, bail out */
	if (!slot_meta)
	{
		if (memslot_base->flags.thread_safe)
			MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");

		return NULL;
	}

	/* Grab address and leave */
	ret_ptr = MemSlotBaseSlotData(slot_meta);

	if (memslot_base->flags.thread_safe)
		MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");

	return ret_ptr;
}
/**************************************************************************************************************************/
void *MemSlotBaseSlotPopHead(MemSlotBase *memslot_base, int list_id)
{
	MemSlotMetaData *slot_meta;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_LOCK(memslot_base->mutex, "MEM_SLOT");

	/* Pop item from linked list and free slot */
	slot_meta = DLinkedListPopHead(&memslot_base->list[list_id]);

	/* Nothing left, bail out */
	if (!slot_meta)
	{
		/* Running THREAD_SAFE, UNLOCK MUTEX */
		if (memslot_base->flags.thread_safe)
			MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");

		return NULL;
	}

	/* Free slot */
	SlotQueueFree(&memslot_base->slots, slot_meta->slot_id);

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");

	return MemSlotBaseSlotData(slot_meta);
}
/**************************************************************************************************************************/
void *MemSlotBaseSlotPopTail(MemSlotBase *memslot_base, int list_id)
{
	MemSlotMetaData *slot_meta;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_LOCK(memslot_base->mutex, "MEM_SLOT");

	/* Pop item from linked list and free slot */
	slot_meta = DLinkedListPopTail(&memslot_base->list[list_id]);

	/* Nothing left, bail out */
	if (!slot_meta)
	{
		/* Running THREAD_SAFE, UNLOCK MUTEX */
		if (memslot_base->flags.thread_safe)
			MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");

		return NULL;
	}

	/* Free slot */
	SlotQueueFree(&memslot_base->slots, slot_meta->slot_id);

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");

	return MemSlotBaseSlotData(slot_meta);
}
/**************************************************************************************************************************/
long MemSlotBaseSlotListSizeAll(MemSlotBase *memslot_base)
{
	long count;
	int i;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_LOCK(memslot_base->mutex, "MEM_SLOT");

	/* Count ALL lists size */
	for (count = 0, i = 0; i < MEMSLOT_DEFAULT_LIST_COUNT; i++)
	{
		count += MemSlotBaseSlotListIDSize(memslot_base, i);
		continue;
	}

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");

	return count;
}
/**************************************************************************************************************************/
long MemSlotBaseSlotListIDSize(MemSlotBase *memslot_base, int list_id)
{
	long list_sz;

	/* Sanity check */
	if (!memslot_base)
		return 0;

	/* List id too big */
	if (list_id >= MEMSLOT_DEFAULT_LIST_COUNT)
		return 0;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_LOCK(memslot_base->mutex, "MEM_SLOT");

	list_sz = memslot_base->list[list_id].size;

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");

	return list_sz;
}
/**************************************************************************************************************************/
int MemSlotBaseSlotListIDSwitchToTail(MemSlotBase *memslot_base, int slot_id, int list_id)
{
	MemSlotMetaData *slot_meta;

	/* Sanity check */
	if (!memslot_base)
		return 0;

	/* List id too big */
	if (list_id >= MEMSLOT_DEFAULT_LIST_COUNT)
		return 0;

	/* Uninitialized SLOT */
	if (slot_id < 0)
		return 0;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_LOCK(memslot_base->mutex, "MEM_SLOT");

	slot_meta = MemArenaGrabByID(memslot_base->arena, slot_id);

	/* Switch lists */
	DLinkedListDelete(&memslot_base->list[slot_meta->cur_list_id], &slot_meta->node);
	DLinkedListAddTail(&memslot_base->list[list_id], &slot_meta->node, slot_meta);

	/* Set to new LIST_ID */
	slot_meta->cur_list_id = list_id;

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");

	return 1;
}
/**************************************************************************************************************************/
int MemSlotBaseSlotListIDSwitch(MemSlotBase *memslot_base, int slot_id, int list_id)
{
	MemSlotMetaData *slot_meta;

	/* Sanity check */
	if (!memslot_base)
		return 0;

	/* Uninitialized SLOT */
	if (slot_id < 0)
		return 0;

	/* List id too big */
	if (list_id >= MEMSLOT_DEFAULT_LIST_COUNT)
		return 0;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_LOCK(memslot_base->mutex, "MEM_SLOT");

	slot_meta = MemArenaGrabByID(memslot_base->arena, slot_id);

	/* Switch lists */
	DLinkedListDelete(&memslot_base->list[slot_meta->cur_list_id], &slot_meta->node);
	DLinkedListAdd(&memslot_base->list[list_id], &slot_meta->node, slot_meta);

	/* Set to new LIST_ID */
	slot_meta->cur_list_id = list_id;

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");

	return 1;
}
/**************************************************************************************************************************/
void *MemSlotBaseSlotData(void *absolute_ptr)
{
	char *ret_ptr;

	/* Nothing loaded, bail out */
	if (!absolute_ptr)
		return NULL;

	/* Calculate return pointer from absolute */
	ret_ptr = absolute_ptr;
	ret_ptr += sizeof(MemSlotMetaData);

	return ret_ptr;
}
/**************************************************************************************************************************/
void *MemSlotBaseSlotGrabByID(MemSlotBase *memslot_base, int slot_id)
{
	MemSlotMetaData *slot_meta;
	char *data_ptr;

	/* Uninitialized SLOT */
	if (slot_id < 0)
		return NULL;

	/* Grab slot METADATA */
	slot_meta				= MemArenaGrabByID(memslot_base->arena, slot_id);

	/* Calculate offset to data space */
	data_ptr				= MemSlotBaseSlotData(slot_meta);

	return data_ptr;
}
/**************************************************************************************************************************/
void *MemSlotBaseSlotGrabAndLeaveLocked(MemSlotBase *memslot_base)
{
	MemSlotMetaData *slot_meta;
	char *data_ptr;
	int slot_id;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_LOCK(memslot_base->mutex, "MEM_SLOT");

	/* Grab a free slot from queue */
	slot_id = SlotQueueGrab(&memslot_base->slots);

	/* No more available slots, bail out */
	if (slot_id < 0)
	{
		/* Running THREAD_SAFE, UNLOCK MUTEX */
		if (memslot_base->flags.thread_safe)
			MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");

		return NULL;
	}

	/* Grab slot METADATA */
	slot_meta				= MemArenaGrabByID(memslot_base->arena, slot_id);
	slot_meta->slot_id		= slot_id;
	slot_meta->cur_list_id	= 0;
	slot_meta->canary00		= (MEMSLOT_CANARY_SEED * (slot_id + 1));
	slot_meta->canary01		= (MEMSLOT_CANARY_SEED + (slot_id + 1));

	/* Calculate offset to data space */
	data_ptr	= (char*)slot_meta;
	data_ptr	+= sizeof(MemSlotMetaData);

	//printf("MemSlotBaseSlotGrab - GRABBED SLOT_ID [%d] - DATA AT [%p] - SLOT_META at [%p]\n", slot_id, data_ptr, slot_meta_ptr);

	/* Always ADD to TAIL OF LIST ZERO */
	DLinkedListAddTail(&memslot_base->list[slot_meta->cur_list_id], &slot_meta->node, slot_meta);

	/* DO NOT UNLOCK MUTEX, CALLER WILL BE RESPONSABLE FOR THAT */
	return data_ptr;
}
/**************************************************************************************************************************/
void *MemSlotBaseSlotGrab(MemSlotBase *memslot_base)
{
	MemSlotMetaData *slot_meta;
	char *data_ptr;
	int slot_id;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_LOCK(memslot_base->mutex, "MEM_SLOT");

	/* Grab a free slot from queue */
	slot_id 				= SlotQueueGrab(&memslot_base->slots);

	/* No more available slots, bail out */
	if (slot_id < 0)
	{
		/* Running THREAD_SAFE, UNLOCK MUTEX */
		if (memslot_base->flags.thread_safe)
			MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");

		return NULL;
	}

	/* Grab slot METADATA */
	slot_meta				= MemArenaGrabByID(memslot_base->arena, slot_id);

	slot_meta->slot_id		= slot_id;
	slot_meta->cur_list_id	= 0;
	slot_meta->canary00		= (MEMSLOT_CANARY_SEED * (slot_id + 1));
	slot_meta->canary01		= (MEMSLOT_CANARY_SEED + (slot_id + 1));

	/* Calculate offset to data space */
	data_ptr				= MemSlotBaseSlotData(slot_meta);

	//printf("MemSlotBaseSlotGrab - GRABBED SLOT_ID [%d] - DATA AT [%p] - SLOT_META at [%p]\n", slot_id, data_ptr, slot_meta_ptr);

	/* Always ADD to TAIL OF LIST ZERO */
	DLinkedListAddTail(&memslot_base->list[slot_meta->cur_list_id], &slot_meta->node, slot_meta);

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");

	return data_ptr;
}
/**************************************************************************************************************************/
int MemSlotBaseSlotGetID(void *slot_ptr)
{
	MemSlotMetaData *slot_meta;
	char *slot_meta_ptr;

	/* Grab slot META on the LEFT of this pointer */
	slot_meta_ptr	= slot_ptr;
	slot_meta_ptr	-= sizeof(MemSlotMetaData);
	slot_meta		= (MemSlotMetaData*)slot_meta_ptr;

	/* Check CANARIES */
	if ( (slot_meta->canary00 != (MEMSLOT_CANARY_SEED * (slot_meta->slot_id + 1))) ||  (slot_meta->canary01 != (MEMSLOT_CANARY_SEED + (slot_meta->slot_id + 1))) )
	{
		assert(0);
		return -1;
	}

	return slot_meta->slot_id;
}
/**************************************************************************************************************************/
int MemSlotBaseSlotFree(MemSlotBase *memslot_base, void *slot_ptr)
{
	MemSlotMetaData *slot_meta;
	char *slot_meta_ptr;

	/* Sanity check */
	if (!slot_ptr)
		return 0;

	/* Grab slot META on the LEFT of this pointer */
	slot_meta_ptr	= slot_ptr;
	slot_meta_ptr	-= sizeof(MemSlotMetaData);
	slot_meta		= (MemSlotMetaData*)slot_meta_ptr;

	/* Check CANARIES */
	if ( (slot_meta->canary00 != (MEMSLOT_CANARY_SEED * (slot_meta->slot_id + 1))) ||  (slot_meta->canary01 != (MEMSLOT_CANARY_SEED + (slot_meta->slot_id + 1))) )
		assert(0);

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_LOCK(memslot_base->mutex, "MEM_SLOT");

	/* Free SLOT and remove from ACTIVE LIST */
	SlotQueueFree(&memslot_base->slots, slot_meta->slot_id);
	DLinkedListDelete(&memslot_base->list[slot_meta->cur_list_id], &slot_meta->node);
	MemArenaReleaseByID(memslot_base->arena, slot_meta->slot_id);

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (memslot_base->flags.thread_safe)
		MUTEX_UNLOCK(memslot_base->mutex, "MEM_SLOT");

	return 1;
}
/**************************************************************************************************************************/



