/*
 * dyn_bitmap.c
 *
 *  Created on: 2014-09-10
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

static int DynBitMapGrowIfNeeded(DynBitMap *dyn_bitmap, long wanted_index);

/**************************************************************************************************************************/
DynBitMap *DynBitMapNew(LibDataThreadSafeType thrd_type, long grow_rate)
{
	DynBitMap *dyn_bitmap;

	/* Create and initialize dynamic bitmap */
	dyn_bitmap = calloc(1, sizeof(DynBitMap));
	DynBitMapInit(dyn_bitmap, thrd_type, grow_rate);

	return dyn_bitmap;
}
/**************************************************************************************************************************/
int DynBitMapInit(DynBitMap *dyn_bitmap, LibDataThreadSafeType thrd_type, long grow_rate)
{
	/* Sanity check */
	if (!dyn_bitmap)
		return 0;

	memset(dyn_bitmap, 0, sizeof(DynBitMap));

	/* Initialize bitmap area */
	dyn_bitmap->bitmap.data			= calloc(1, (grow_rate + 1));

	/* Initialize PTHREAD MUTEX */
	dyn_bitmap->mutex				= (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;

	/* Populate data */
	dyn_bitmap->bitmap.grow_rate	= grow_rate;
	dyn_bitmap->bitmap.capacity		= grow_rate;
	dyn_bitmap->bitmap.higher_seen	= 0;
	dyn_bitmap->flags.thread_safe	= (BRBDATA_THREAD_SAFE == thrd_type) ? 1 : 0;

	return 1;
}
/**************************************************************************************************************************/
int DynBitMapDestroy(DynBitMap *dyn_bitmap)
{
	/* Sanity check */
	if (!dyn_bitmap)
		return 0;

	/* Clean up and destroy */
	DynBitMapClean(dyn_bitmap);
	free(dyn_bitmap);

	return 1;
}
/**************************************************************************************************************************/
int DynBitMapClean(DynBitMap *dyn_bitmap)
{
	/* Sanity check */
	if (!dyn_bitmap)
		return 0;

	/* Running thread safe, destroy MUTEX */
	if (dyn_bitmap->flags.thread_safe)
		MUTEX_DESTROY(dyn_bitmap->mutex, "DYN_BITMAP");

	/* Reset PTHREAD MUTEX */
	dyn_bitmap->mutex				= (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;

	/* Destroy bitmap data */
	free(dyn_bitmap->bitmap.data);

	return 1;
}
/**************************************************************************************************************************/
int DynBitMapBitSet(DynBitMap *dyn_bitmap, long index)
{
	int need_grow;
	int bit_state;

	/* Sanity check */
	if (!dyn_bitmap)
		return 0;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (dyn_bitmap->flags.thread_safe)
		MUTEX_LOCK(dyn_bitmap->mutex, "DYN_BITMAP");

	/* Update higher seen */
	if (dyn_bitmap->bitmap.higher_seen < index)
		dyn_bitmap->bitmap.higher_seen = index;

	/* Grow if needed, and SET BIT */
	need_grow = DynBitMapGrowIfNeeded(dyn_bitmap, index);

	/* Test BITMAP and SET */
	bit_state = BITMASK_TEST(dyn_bitmap->bitmap.data, index);
	BITMASK_SET(dyn_bitmap->bitmap.data, index);

	/* Bit state CHANGE, update counter */
	if (!bit_state)
		dyn_bitmap->bitmap.bitset_count++;

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (dyn_bitmap->flags.thread_safe)
		MUTEX_UNLOCK(dyn_bitmap->mutex, "DYN_BITMAP");

	return 1;
}
/**************************************************************************************************************************/
int DynBitMapBitTest(DynBitMap *dyn_bitmap, long index)
{
	int bit_state;

	/* Sanity check */
	if (!dyn_bitmap)
		return 0;

	/* Never seen this high */
	if (dyn_bitmap->bitmap.higher_seen < index)
		return 0;

	/* Test BITMAP */
	bit_state = BITMASK_TEST(dyn_bitmap->bitmap.data, index);

	return bit_state;
}
/**************************************************************************************************************************/
int DynBitMapBitClear(DynBitMap *dyn_bitmap, long index)
{
	int bit_state;

	/* Sanity check */
	if (!dyn_bitmap)
		return 0;

	/* Never seen this high */
	if (dyn_bitmap->bitmap.higher_seen < index)
		return 0;

	/* Test BITMAP and clear */
	bit_state = BITMASK_TEST(dyn_bitmap->bitmap.data, index);
	BITMASK_CLR(dyn_bitmap->bitmap.data, index);

	/* Bit state CHANGE, update counter */
	if (bit_state)
		dyn_bitmap->bitmap.bitset_count--;

	return 1;
}
/**************************************************************************************************************************/
int DynBitMapBitClearAll(DynBitMap *dyn_bitmap)
{
	/* Sanity check */
	if (!dyn_bitmap)
		return 0;

	/* Clean map */
	memset(dyn_bitmap->bitmap.data, 0, dyn_bitmap->bitmap.capacity);

	return 1;
}
/**************************************************************************************************************************/
int DynBitMapCompare(DynBitMap *dyn_bitmap_one, DynBitMap *dyn_bitmap_two)
{
	int smaller_cap;
	int is_equal;

	/* Sanity check */
	if ((!dyn_bitmap_one) || (!dyn_bitmap_two))
		return 0;

	/* Higher seen differ, bail out */
	if (dyn_bitmap_one->bitmap.higher_seen != dyn_bitmap_two->bitmap.higher_seen)
		return 0;

	/* Grab the smaller capacity and compare */
	smaller_cap = (dyn_bitmap_one->bitmap.capacity < dyn_bitmap_two->bitmap.capacity) ? dyn_bitmap_one->bitmap.capacity : dyn_bitmap_two->bitmap.capacity;
	is_equal	= (!memcmp(dyn_bitmap_one->bitmap.data, dyn_bitmap_two->bitmap.data, smaller_cap));

	return is_equal;
}
/**************************************************************************************************************************/
long DynBitMapGetHigherSeen(DynBitMap *dyn_bitmap)
{
	/* Sanity check */
	if (!dyn_bitmap)
		return 0;

	return dyn_bitmap->bitmap.higher_seen;
}
/**************************************************************************************************************************/
int DynBitMapCheckMultiBlocks(DynBitMap *dyn_bitmap, long block_idx_begin, long block_idx_finish)
{
	long i;

	/* Sanity check */
	if (!dyn_bitmap)
		return 0;

	/* Test all BITs */
	for (i = block_idx_begin; i <= block_idx_finish; i++)
	{
		/* BIT failed, give up */
		if (!DynBitMapBitTest(dyn_bitmap, i))
			return 0;

		continue;
	}

	/* ALL OK */
	return 1;
}
/**************************************************************************************************************************/
int DynBitMapGetValidBlocks(DynBitMap *dyn_bitmap, long block_idx_begin, long block_idx_finish, DynBitMapValidBlockIdx *valid_block_idx)
{
	int i;
	int found_first = 0;

	/* Initialize values */
	valid_block_idx->first_idx 	= -1;
	valid_block_idx->last_idx 	= -1;

	/* Sanity check */
	if (!dyn_bitmap)
		return 0;

	/* Sanity check values */
	if (block_idx_begin > dyn_bitmap->bitmap.higher_seen)
		return 0;

	if (block_idx_finish > dyn_bitmap->bitmap.higher_seen)
		block_idx_finish = dyn_bitmap->bitmap.higher_seen;

	/* Find first valid block */
	for (i = block_idx_begin; i <= block_idx_finish; i++)
	{
		/* Searching for first valid block */
		if (!found_first)
		{
			/* BIT success, get index and mark flag to search the last valid block */
			if (DynBitMapBitTest(dyn_bitmap, i))
			{
				valid_block_idx->first_idx 	= i;
				valid_block_idx->last_idx 	= i;

				found_first = 1;
			}
		}
		/* Continue searching until we have valid blocks */
		else
		{
			/* BIT failed, give up */
			if (!DynBitMapBitTest(dyn_bitmap, i))
			{
				valid_block_idx->last_idx	= (i-1);
				break;
			}

			valid_block_idx->last_idx		= i;
		}

		continue;
	}

	/* We found any valid block? */
	return (valid_block_idx->first_idx < 0 ? 0 : 1);
}
/**************************************************************************************************************************/
int DynBitMapGenerateStringMap(DynBitMap *dyn_bitmap, char *ret_mask, int ret_mask_size)
{
	int active_factor_mod;
	int active_factor;
	int bit_active;
	long i;
	long j;

	int cc						= 0;
	int ci						= 0;
	int step_idx 				= 0;
	int step_offset 			= 0;

	/* Sanity check */
	if (!dyn_bitmap || dyn_bitmap->bitmap.higher_seen <= 0)
	{
		//printf("DynBitMapGenerateStringMap - Will not calculate\n");
		return 0;
	}

	/* Calculate how many continuous blocks will set a single mask byte */
	active_factor 		= (dyn_bitmap->bitmap.higher_seen < ret_mask_size) ? 1 : (dyn_bitmap->bitmap.higher_seen / ret_mask_size);
	active_factor_mod	= (dyn_bitmap->bitmap.higher_seen  % ret_mask_size);

	/* Walk all DOTs */
	for (i = 0; i < dyn_bitmap->bitmap.higher_seen; i++)
	{
		bit_active	= DynBitMapBitTest(dyn_bitmap, i);
		cc			+= bit_active ? 1 : 0;

		/* Its time to paint some rows */
		if (((i % active_factor) == 0) || (i == dyn_bitmap->bitmap.higher_seen))
		{
			/* Calculate from where on the string mask we will start painting */
			step_offset = (((i + 1) * ret_mask_size) / dyn_bitmap->bitmap.higher_seen);

			/* Print in chars the progress */
			for (ci = step_idx; ci < step_offset, ci < ret_mask_size; ci++)
				ret_mask[ci]	= cc > 0 ? '1' : '0';

			/* Reset state */
			cc 			= 0;
			step_idx	= step_offset;
		}
		continue;
	}

	return 1;
}
/**************************************************************************************************************************/
int DynBitMapMetaDataPack(DynBitMap *dyn_bitmap, MetaData *dst_metadata, long item_sub_id)
{
	/* Sanity check */
	if (!dyn_bitmap)
		return 0;

	/* Clean up stack */
	memset(&dyn_bitmap->metadata, 0, sizeof(DynBitMapMetaData));

	/* Populate DYN_BITMAP_META */
	dyn_bitmap->metadata.capacity		= dyn_bitmap->bitmap.capacity;
	dyn_bitmap->metadata.grow_rate		= dyn_bitmap->bitmap.grow_rate;
	dyn_bitmap->metadata.higher_seen	= dyn_bitmap->bitmap.higher_seen;
	dyn_bitmap->metadata.bitset_count	= dyn_bitmap->bitmap.bitset_count;

	//printf("DynBitMapMetaDataPack - CAP [%ld] - GROW [%ld] - HIGHER [%ld] - BITSET_COUNT [%ld]\n",
	//		dyn_bitmap->metadata.capacity, dyn_bitmap->metadata.grow_rate, dyn_bitmap->metadata.higher_seen, dyn_bitmap->bitmap.bitset_count);

	/* Dump data to METADATA */
	MetaDataItemAdd(dst_metadata, DATATYPE_DYN_BITMAP_META, item_sub_id, &dyn_bitmap->metadata, sizeof(DynBitMapMetaData));
	MetaDataItemAdd(dst_metadata, DATATYPE_DYN_BITMAP_DATA, item_sub_id, dyn_bitmap->bitmap.data, dyn_bitmap->bitmap.capacity);

	return 1;
}
/**************************************************************************************************************************/
int DynBitMapMetaDataUnPack(DynBitMap *dyn_bitmap, MemBuffer *raw_metadata_mb)
{
	DynBitMapMetaData *dyn_bitmap_meta;
	MetaDataItem *meta_item;
	MetaData metadata;
	int op_status;
	int i;

	/* Sanity check */
	if (!dyn_bitmap)
		return 0;

	/* Clean up stack and unpack METADATA */
	memset(&metadata, 0, sizeof(MetaData));
	op_status = MetaDataUnpack(&metadata, raw_metadata_mb, NULL);

	/* Failed unpacking */
	if (METADATA_UNPACK_SUCCESS != op_status)
	{
		/* Clean UP METDATA and leave */
		MetaDataClean(&metadata);
		return (-op_status);
	}

	/* Now walk all items */
	for (i = metadata.item_offset; i < metadata.items.count; i++)
	{
		meta_item = MemArenaGrabByID(metadata.items.arena, i);

		/* DYN_BITMAP METADATA, populate back */
		if (DATATYPE_DYN_BITMAP_META == meta_item->item_id)
		{
			dyn_bitmap_meta = meta_item->ptr;

			/* Populate data */
			dyn_bitmap->bitmap.capacity		= dyn_bitmap_meta->capacity;
			dyn_bitmap->bitmap.grow_rate	= dyn_bitmap_meta->grow_rate;
			dyn_bitmap->bitmap.higher_seen	= dyn_bitmap_meta->higher_seen;
			dyn_bitmap->bitmap.bitset_count	= dyn_bitmap_meta->bitset_count;

			//printf("DynBitMapMetaDataUnPack - DATATYPE_DYN_BITMAP_META with [%d] bytes - CAP [%ld] - GROW [%ld] - HIGHER [%ld] - BITSET_COUNT [%ld]\n",
			//		meta_item->sz, dyn_bitmap_meta->capacity, dyn_bitmap_meta->grow_rate, dyn_bitmap_meta->higher_seen, dyn_bitmap_meta->bitset_count);

			continue;
		}
		/* DYN_BITMAP RAW_DATA, copy */
		else if (DATATYPE_DYN_BITMAP_DATA == meta_item->item_id)
		{
			/* Free bitmap if it already exists */
			if (dyn_bitmap->bitmap.data)
				free(dyn_bitmap->bitmap.data);

			//printf("DynBitMapMetaDataUnPack - DATATYPE_DYN_BITMAP_DATA with [%d] bytes\n", meta_item->sz);

			/* Create a new one and copy back BITMAP from METADATA */
			dyn_bitmap->bitmap.data = calloc(1, (meta_item->sz + 1));
			memcpy(dyn_bitmap->bitmap.data, meta_item->ptr, meta_item->sz);

			continue;
		}

		continue;
	}

	/* Clean UP METDATA and leave */
	MetaDataClean(&metadata);
	return 1;
}
/**************************************************************************************************************************/
MemBuffer *DynBitMapMetaDataPackToMB(DynBitMap *dyn_bitmap, long item_sub_id)
{
	MetaData metadata;
	MemBuffer *packed_mb = MemBufferNew(BRBDATA_THREAD_UNSAFE, 1024);

	/* Sanity check */
	if (!dyn_bitmap)
		return NULL;

	/* Clean up stack */
	memset(&metadata, 0, sizeof(MetaData));

	/* Pack data into METADATA SERIALIZER and DUMP it into MEMBUFFER */
	DynBitMapMetaDataPack(dyn_bitmap, &metadata, item_sub_id);
	MetaDataPack(&metadata, packed_mb);

	/* Clean UP METDATA and leave */
	MetaDataClean(&metadata);

	return packed_mb;
}
/**************************************************************************************************************************/
DynBitMap *DynBitMapMetaDataUnPackFromMeta(MetaData *metadata)
{
	DynBitMapMetaData *dyn_bitmap_meta;
	MetaDataItem *meta_item;
	DynBitMap *dyn_bitmap;
	int i;

	/* Create a new bitmap and unpack */
	dyn_bitmap	= DynBitMapNew(BRBDATA_THREAD_UNSAFE, 128);

	/* Now walk all items */
	for (i = metadata->item_offset; i < metadata->items.count; i++)
	{
		meta_item = MemArenaGrabByID(metadata->items.arena, i);

		/* DYN_BITMAP METADATA, populate back */
		if (DATATYPE_DYN_BITMAP_META == meta_item->item_id)
		{
			dyn_bitmap_meta = meta_item->ptr;

			/* Populate data */
			dyn_bitmap->bitmap.capacity		= dyn_bitmap_meta->capacity;
			dyn_bitmap->bitmap.grow_rate	= dyn_bitmap_meta->grow_rate;
			dyn_bitmap->bitmap.higher_seen	= dyn_bitmap_meta->higher_seen;

			//printf("DynBitMapMetaDataUnPackFromMeta - DATATYPE_DYN_BITMAP_META with [%d] bytes - CAP [%ld] - GROW [%ld] - HIGHER [%ld]\n",
			//		meta_item->sz, dyn_bitmap_meta->capacity, dyn_bitmap_meta->grow_rate, dyn_bitmap_meta->higher_seen);

			continue;
		}
		/* DYN_BITMAP RAW_DATA, copy */
		else if (DATATYPE_DYN_BITMAP_DATA == meta_item->item_id)
		{
			/* Free bitmap if it already exists */
			if (dyn_bitmap->bitmap.data)
				free(dyn_bitmap->bitmap.data);

			//	printf("DynBitMapMetaDataUnPackFromMeta - DATATYPE_DYN_BITMAP_DATA with [%d] bytes\n", meta_item->sz);

			/* Create a new one and copy back BITMAP from METADATA */
			dyn_bitmap->bitmap.data = calloc(1, (meta_item->sz + 1));
			memcpy(dyn_bitmap->bitmap.data, meta_item->ptr, meta_item->sz);

			continue;
		}

		continue;
	}

	return dyn_bitmap;
}
/**************************************************************************************************************************/
DynBitMap *DynBitMapMetaDataUnPackFromMB(MemBuffer *raw_metadata_mb)
{
	DynBitMap *dyn_bitmap;
	int op_status;

	/* Create a new bitmap and unpack */
	dyn_bitmap	= DynBitMapNew(BRBDATA_THREAD_UNSAFE, 128);
	op_status	= DynBitMapMetaDataUnPack(dyn_bitmap, raw_metadata_mb);

	/* Failed unpacking METADATA, destroy and LEAVE */
	if (op_status <= 0)
	{
		//printf("DynBitMapMetaDataUnPackFromMB - Failed unpacking BITMAP\n");
		DynBitMapDestroy(dyn_bitmap);
		return NULL;
	}

	return dyn_bitmap;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int DynBitMapGrowIfNeeded(DynBitMap *dyn_bitmap, long wanted_index)
{
	char *tmp_ptr;
	long wanted_offset	= (((wanted_index + 7) / 8) + 1);
	long new_size		= (wanted_offset + dyn_bitmap->bitmap.grow_rate);

	/* Wanted offset is beyond our capacity, grow */
	if (dyn_bitmap->bitmap.capacity < wanted_offset)
	{
		//printf("DynBitMapGrowIfNeeded - Will grow - CAP [%ld] - WANTED [%ld]\n", dyn_bitmap->bitmap.capacity, wanted_offset);

		/* REALLOC and initialize */
		dyn_bitmap->bitmap.data = realloc(dyn_bitmap->bitmap.data, new_size);
		memset(&dyn_bitmap->bitmap.data[dyn_bitmap->bitmap.capacity], 0, (new_size - dyn_bitmap->bitmap.capacity));

		/* Update to new size */
		dyn_bitmap->bitmap.capacity = new_size;

		return 1;
	}

	return 0;
}
/**************************************************************************************************************************/
