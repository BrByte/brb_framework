/*
 * mem_arena.c
 *
 *  Created on: 2013-01-18
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

static void MemArenaSlotHeaderInit(MemArena *mem_arena, MemArenaSlotHeader *header, long slot_id);
static void MemArenaSlotHeaderClean(MemArenaSlotHeader *header);

/**************************************************************************************************************************/
/* Context arena control functions
/**************************************************************************************************************************/
MemArena *MemArenaNew(long arena_size, long slot_size, long slot_count, int type)
{
	MemArena *mem_arena = calloc(1, sizeof(MemArena));

	/* Initialize data arena */
	mem_arena->data							= calloc(arena_size + 1, sizeof(void*));
	mem_arena->mutex						= (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;

	mem_arena->size[MEMARENA_SIZE_CAPACITY]	= arena_size;
	mem_arena->size[MEMARENA_SIZE_INITIAL]	= arena_size;
	mem_arena->size[MEMARENA_SIZE_CURRENT]	= 0;

	mem_arena->slot[MEMARENA_SLOT_SIZE]		= slot_size;
	mem_arena->slot[MEMARENA_SLOT_COUNT]	= slot_count;

	/* Set flags we are running THREAD SAFE */
	if (BRBDATA_THREAD_SAFE == type)
		mem_arena->flags.thread_safe = 1;

	return mem_arena;
}
/**************************************************************************************************************************/
void MemArenaClean(MemArena *mem_arena)
{
	long area_cap;
	char *raw_data;
	long i;

	/* Sanity check */
	if (!mem_arena)
		return;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (mem_arena->flags.thread_safe)
		MUTEX_LOCK(mem_arena->mutex, "MEM_ARENA");

	area_cap	= ((mem_arena->slot[MEMARENA_SLOT_COUNT] + 1) * (mem_arena->slot[MEMARENA_SLOT_SIZE] + 1));

	/* Free all arenas */
	for (i = 0; i < mem_arena->size[MEMARENA_SIZE_CAPACITY]; i++)
	{
		/* Clean whole slot block */
		if (mem_arena->data[i])
		{
			/* Calculate data ADDR and clean */
			raw_data = (mem_arena->data[i] + sizeof(MemArenaSlotHeader));
			memset(raw_data, 0, area_cap);
		}

		continue;
	}

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (mem_arena->flags.thread_safe)
		MUTEX_UNLOCK(mem_arena->mutex, "MEM_ARENA");

	return;
}
/**************************************************************************************************************************/
void MemArenaDestroy(MemArena *mem_arena)
{
	MemArenaSlotHeader *header;
	long i;

	/* Sanity check */
	if (!mem_arena)
		return;

	/* Free all arenas */
	for (i = 0; i < mem_arena->size[MEMARENA_SIZE_CAPACITY]; i++)
	{
		if (mem_arena->data[i])
		{
			header = (MemArenaSlotHeader *)mem_arena->data[i];
			MemArenaSlotHeaderClean(header);
			free(mem_arena->data[i]);
		}

		mem_arena->data[i] = NULL;
		continue;
	}

	/* Free arena control block */
	free(mem_arena->data);

	/* Running thread safe, destroy MUTEX */
	if (mem_arena->flags.thread_safe)
		MUTEX_DESTROY(mem_arena->mutex, "MEM_ARENA");

	free(mem_arena);

	return;
}
/**************************************************************************************************************************/
void *MemArenaFindByID(MemArena *mem_arena, long id)
{
	MemArenaSlotHeader *header;
	void *ret_ptr;
	long arena_id;
	long slot_offset;

	/* Sanity check */
	if (id < 0)
		return NULL;

	/* Sanity check */
	if (!mem_arena)
		return NULL;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (mem_arena->flags.thread_safe)
		MUTEX_LOCK(mem_arena->mutex, "MEM_ARENA");

	/* Calculate arena_id and slot_offset */
	arena_id		= (id / mem_arena->slot[MEMARENA_SLOT_COUNT]);
	slot_offset		= (id % (mem_arena->slot[MEMARENA_SLOT_COUNT]));

	/* Need to grow arena */
	if (arena_id >= mem_arena->size[MEMARENA_SIZE_CAPACITY])
		goto leave;

	/* Check into arena if there is a slot */
	if (!mem_arena->data[arena_id])
		goto leave;

	/* Dereference HEADER on the begin of ARENA AREA */
	header = (MemArenaSlotHeader *)(mem_arena->data[arena_id]);

	/* Not in use, bail out */
	if (!DynBitMapBitTest(header->bitmap, slot_offset))
		goto leave;

	/* Calculate SLOT_PTR to be returned and leave */
	ret_ptr = &mem_arena->data[arena_id][(slot_offset * mem_arena->slot[MEMARENA_SLOT_SIZE])];
	ret_ptr += sizeof(MemArenaSlotHeader);

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (mem_arena->flags.thread_safe)
		MUTEX_UNLOCK(mem_arena->mutex, "MEM_ARENA");

	return ret_ptr;

	leave:
	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (mem_arena->flags.thread_safe)
		MUTEX_UNLOCK(mem_arena->mutex, "MEM_ARENA");
	return NULL;
}
/**************************************************************************************************************************/
void *MemArenaGrabByID(MemArena *mem_arena, long id)
{
	MemArenaSlotHeader *header;
	void *ret_ptr;
	long new_cap;
	long area_cap;
	long arena_id;
	long slot_offset;

	/* Sanity check */
	if (id < 0)
		return NULL;

	/* Sanity check */
	if (!mem_arena)
		return NULL;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (mem_arena->flags.thread_safe)
		MUTEX_LOCK(mem_arena->mutex, "MEM_ARENA");

	/* Calculate arena_id and slot_offset */
	arena_id		= (id / mem_arena->slot[MEMARENA_SLOT_COUNT]);
	slot_offset		= (id % (mem_arena->slot[MEMARENA_SLOT_COUNT]));

	/* Need to grow arena */
	if (arena_id >= mem_arena->size[MEMARENA_SIZE_CAPACITY])
	{
		/* Calculate and re_alloc new arena capacity */
		new_cap			= (arena_id + mem_arena->size[MEMARENA_SIZE_INITIAL]);
		mem_arena->data	= realloc(mem_arena->data, ((new_cap + 1) * sizeof(void*)));

		/* Clean up arena array of pointers, and update capacity */
		memset(&mem_arena->data[mem_arena->size[MEMARENA_SIZE_CAPACITY]], 0, ( (new_cap - mem_arena->size[MEMARENA_SIZE_CAPACITY]) * sizeof(void*)) );
		mem_arena->size[MEMARENA_SIZE_CAPACITY] = new_cap;
	}

	/* Check into arena if there is a slot */
	if (!mem_arena->data[arena_id])
	{
		/* Create and account a new slot into arena */
		area_cap					= (sizeof(MemArenaSlotHeader) + ((mem_arena->slot[MEMARENA_SLOT_COUNT] + 1) * (mem_arena->slot[MEMARENA_SLOT_SIZE] + 1)));
		mem_arena->data[arena_id]	= calloc(1, area_cap);
		mem_arena->size[MEMARENA_SIZE_CURRENT]++;

		/* Dereference HEADER on the begin of ARENA AREA */
		header				= (MemArenaSlotHeader *)mem_arena->data[arena_id];
		MemArenaSlotHeaderInit(mem_arena, header, arena_id);
	}

	/* Dereference HEADER on the begin of ARENA AREA */
	header = (MemArenaSlotHeader *)mem_arena->data[arena_id];

	/* Make sure canaries are OK */
	assert(0xDE == header->canary00);
	assert(0xAD == header->canary01);

	/* Was not in use, increment busy count */
	if (!DynBitMapBitTest(header->bitmap, slot_offset))
		header->busy_count++;

	/* Set as IN USE */
	DynBitMapBitSet(header->bitmap, slot_offset);

	/* Calculate SLOT_PTR to be returned and leave */
	ret_ptr = &mem_arena->data[arena_id][(slot_offset * mem_arena->slot[MEMARENA_SLOT_SIZE])];
	ret_ptr += sizeof(MemArenaSlotHeader);

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (mem_arena->flags.thread_safe)
		MUTEX_UNLOCK(mem_arena->mutex, "MEM_ARENA");

	return ret_ptr;
}
/**************************************************************************************************************************/
void MemArenaReleaseByID(MemArena *mem_arena, long id)
{
	MemArenaSlotHeader *header;
	long arena_id;
	long slot_offset;

	/* Sanity check */
	if (id < 0)
		return;

	/* Sanity check */
	if (!mem_arena)
		return;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (mem_arena->flags.thread_safe)
		MUTEX_LOCK(mem_arena->mutex, "MEM_ARENA");

	/* Calculate arena_id and slot_offset */
	arena_id		= (id / mem_arena->slot[MEMARENA_SLOT_COUNT]);
	slot_offset		= (id % (mem_arena->slot[MEMARENA_SLOT_COUNT]));

	/* Make sure this arena is ACTIVE and dereference HEADER on the begin of ARENA AREA */
	assert(mem_arena->data[arena_id]);
	header = (MemArenaSlotHeader *)mem_arena->data[arena_id];

	/* Make sure canaries are OK */
	assert(0xDE == header->canary00);
	assert(0xAD == header->canary01);

	/* Make sure this SLOT is in use, and decrement busy count */
	assert(DynBitMapBitTest(header->bitmap, slot_offset));
	DynBitMapBitClear(header->bitmap, slot_offset);
	header->busy_count--;

	/* No more items in use, release SLOT */
	if (0 == header->busy_count)
	{
		MemArenaSlotHeaderClean(header);
		free(mem_arena->data[arena_id]);
		mem_arena->data[arena_id] = NULL;
	}

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (mem_arena->flags.thread_safe)
		MUTEX_UNLOCK(mem_arena->mutex, "MEM_ARENA");

	return;
}
/**************************************************************************************************************************/
void MemArenaLockByID(MemArena *mem_arena, long id)
{
	MemArenaSlotHeader *header;
	long arena_id;

	/* Sanity check */
	if (id < 0)
		return;

	/* Sanity check */
	if (!mem_arena)
		return;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (mem_arena->flags.thread_safe)
		MUTEX_LOCK(mem_arena->mutex, "MEM_ARENA");

	/* Calculate arena_id and slot_offset */
	arena_id		= (id / mem_arena->slot[MEMARENA_SLOT_COUNT]);

	/* Make sure this arena is ACTIVE and dereference HEADER on the begin of ARENA AREA */
	assert(mem_arena->data[arena_id]);
	header = (MemArenaSlotHeader *)mem_arena->data[arena_id];

	/* Make sure canaries are OK */
	assert(0xDE == header->canary00);
	assert(0xAD == header->canary01);
	header->busy_count++;

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (mem_arena->flags.thread_safe)
		MUTEX_UNLOCK(mem_arena->mutex, "MEM_ARENA");

	return;
}
/**************************************************************************************************************************/
int MemArenaToJsonMemBuffer(MemArena *mem_arena, MemBuffer *json_reply_mb)
{
	if (!mem_arena || !json_reply_mb)
		return -1;

	/* Size */
	MEMBUFFER_JSON_ADD_LONG(json_reply_mb, "size_cur", mem_arena->size[MEMARENA_SIZE_CURRENT]);
	MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
	MEMBUFFER_JSON_ADD_LONG(json_reply_mb, "size_init", mem_arena->size[MEMARENA_SIZE_INITIAL]);
	MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
	MEMBUFFER_JSON_ADD_LONG(json_reply_mb, "size_capacity", mem_arena->size[MEMARENA_SIZE_CAPACITY]);
	MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);

	/* Slot */
	MEMBUFFER_JSON_ADD_ULONG(json_reply_mb, "list_size", mem_arena->slot_list.size);
	MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
	MEMBUFFER_JSON_ADD_LONG(json_reply_mb, "slot_size", mem_arena->slot[MEMARENA_SLOT_SIZE]);
	MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
	MEMBUFFER_JSON_ADD_LONG(json_reply_mb, "slot_count", mem_arena->slot[MEMARENA_SLOT_COUNT]);

	return 0;
}
/**************************************************************************************************************************/
int MemArenaUnlockByID(MemArena *mem_arena, long id)
{
	MemArenaSlotHeader *header;
	long arena_id;

	/* Sanity check */
	if (id < 0)
		return 0;

	/* Sanity check */
	if (!mem_arena)
		return 0;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (mem_arena->flags.thread_safe)
		MUTEX_LOCK(mem_arena->mutex, "MEM_ARENA");

	/* Calculate arena_id and slot_offset */
	arena_id		= (id / mem_arena->slot[MEMARENA_SLOT_COUNT]);

	/* Make sure this arena is ACTIVE and dereference HEADER on the begin of ARENA AREA */
	assert(mem_arena->data[arena_id]);
	header = (MemArenaSlotHeader *)mem_arena->data[arena_id];

	/* Make sure canaries are OK */
	assert(0xDE == header->canary00);
	assert(0xAD == header->canary01);
	header->busy_count--;

	/* No more items in use, release SLOT */
	if (0 == header->busy_count)
	{
		MemArenaSlotHeaderClean(header);
		free(mem_arena->data[arena_id]);
		mem_arena->data[arena_id] = NULL;

		/* Running THREAD_SAFE, UNLOCK MUTEX */
		if (mem_arena->flags.thread_safe)
			MUTEX_UNLOCK(mem_arena->mutex, "MEM_ARENA");

		/* Return true to let caller know we destroyed this bucket */
		return 1;
	}

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (mem_arena->flags.thread_safe)
		MUTEX_UNLOCK(mem_arena->mutex, "MEM_ARENA");

	return 0;
}
/**************************************************************************************************************************/
long MemArenaGetArenaCapacity(MemArena *mem_arena)
{
	long result;

	/* Sanity check */
	if (!mem_arena)
		return 0;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (mem_arena->flags.thread_safe)
		MUTEX_LOCK(mem_arena->mutex, "MEM_ARENA");

	result = mem_arena->size[MEMARENA_SIZE_CAPACITY];

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (mem_arena->flags.thread_safe)
		MUTEX_UNLOCK(mem_arena->mutex, "MEM_ARENA");

	return result;
}
/**************************************************************************************************************************/
long MemArenaGetSlotSize(MemArena *mem_arena)
{
	long result;

	/* Sanity check */
	if (!mem_arena)
		return 0;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (mem_arena->flags.thread_safe)
		MUTEX_LOCK(mem_arena->mutex, "MEM_ARENA");

	result = mem_arena->size[MEMARENA_SLOT_SIZE];

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (mem_arena->flags.thread_safe)
		MUTEX_UNLOCK(mem_arena->mutex, "MEM_ARENA");

	return result;
}
/**************************************************************************************************************************/
long MemArenaGetSlotCount(MemArena *mem_arena)
{
	long result;

	/* Sanity check */
	if (!mem_arena)
		return 0;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (mem_arena->flags.thread_safe)
		MUTEX_LOCK(mem_arena->mutex, "MEM_ARENA");

	result = mem_arena->size[MEMARENA_SLOT_COUNT];

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (mem_arena->flags.thread_safe)
		MUTEX_UNLOCK(mem_arena->mutex, "MEM_ARENA");

	return result;
}
/**************************************************************************************************************************/
long MemArenaGetSlotActiveCount(MemArena *mem_arena)
{
	long result;

	/* Sanity check */
	if (!mem_arena)
		return 0;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (mem_arena->flags.thread_safe)
		MUTEX_LOCK(mem_arena->mutex, "MEM_ARENA");

	result = mem_arena->size[MEMARENA_SIZE_CURRENT];

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (mem_arena->flags.thread_safe)
		MUTEX_UNLOCK(mem_arena->mutex, "MEM_ARENA");

	return result;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void MemArenaSlotHeaderInit(MemArena *mem_arena, MemArenaSlotHeader *header, long slot_id)
{
	/* Create a new BITMAP for the SLOT HEADER */
	header->bitmap		= DynBitMapNew(BRBDATA_THREAD_UNSAFE, 64);
	header->mem_arena	= mem_arena;
	header->slot_id		= slot_id;
	header->canary00	= 0xDE;
	header->canary01	= 0xAD;

	/* Add to active SLOT LIST */
	DLinkedListAdd(&mem_arena->slot_list, &header->node, header);
	return;
}
/**************************************************************************************************************************/
static void MemArenaSlotHeaderClean(MemArenaSlotHeader *header)
{
	MemArena *mem_arena = header->mem_arena;

	/* Make sure canaries are OK */
	assert(0xDE == header->canary00);
	assert(0xAD == header->canary01);

	/* Destroy bitmap and delete from active slot list */
	DynBitMapDestroy(header->bitmap);
	DLinkedListDelete(&mem_arena->slot_list, &header->node);
	header->bitmap		= NULL;
	header->canary00	= -1;
	header->canary01	= -1;
	return;
}
/**************************************************************************************************************************/


