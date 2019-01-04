/*
 * mem_buf_maped.c
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

static int MemBufferMappedAlignedWrite(MemBufferMapped *mapped_mb, char *data, long data_sz, long block_idx);
static int MemBufferMappedPartialDataProcess(MemBufferMapped *mapped_mb, char *data, long data_sz, long block_idx, long rel_offset);
static int MemBufferMappedPartialDrop(MemBufferMapped *mapped_mb, MemBuffer *page_part_mb);

/**************************************************************************************************************************/
MemBufferMapped *MemBufferMappedNew(LibDataThreadSafeType thrd_type, int page_size)
{
	MemBufferMapped *mapped_mb;

	mapped_mb = calloc(1, sizeof(MemBufferMapped));

	/* Invoke initialization procedure */
	MemBufferMappedInit(mapped_mb, thrd_type, page_size);

	return mapped_mb;

}
/**************************************************************************************************************************/
int MemBufferMappedInit(MemBufferMapped *mapped_mb, LibDataThreadSafeType thrd_type, int page_size)
{
	/* Sanity check */
	if (!mapped_mb)
		return 0;

	/* Clean up space */
	memset(mapped_mb, 0, sizeof(MemBufferMapped));

	/* Create MAIN MB and MAIN_BITMAP */
	mapped_mb->main_mb				= MemBufferNew(BRBDATA_THREAD_UNSAFE, page_size);
	mapped_mb->dyn_bitmap			= DynBitMapNew(BRBDATA_THREAD_UNSAFE, 256);

	/* Initialize PTHREAD MUTEX and populate data */
	mapped_mb->mutex				= (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	mapped_mb->page_sz				= page_size;
	mapped_mb->flags.thread_safe	= (BRBDATA_THREAD_SAFE == thrd_type) ? 1 : 0;

	/* Initialize partial data list */
	DLinkedListInit(&mapped_mb->partial_mb_list, BRBDATA_THREAD_UNSAFE);

	return 1;
}
/**************************************************************************************************************************/
int MemBufferMappedDestroy(MemBufferMapped *mapped_mb)
{
	/* Sanity check */
	if (!mapped_mb)
		return 0;

	/* Clean up and destroy */
	MemBufferMappedClean(mapped_mb);
	free(mapped_mb);

	return 1;

}
/**************************************************************************************************************************/
int MemBufferMappedClean(MemBufferMapped *mapped_mb)
{
	/* Sanity check */
	if (!mapped_mb)
		return 0;

	/* Running thread safe, destroy MUTEX */
	if (mapped_mb->flags.thread_safe)
		MUTEX_DESTROY(mapped_mb->mutex, "MAPPED_MEMBUFFER");

	/* Clean up all partial data buffers */
	MemBufferMappedPartialClean(mapped_mb);

	/* Destroy MAIN BUFFER and BITMAP */
	MemBufferDestroy(mapped_mb->main_mb);
	DynBitMapDestroy(mapped_mb->dyn_bitmap);

	/* Reset DATA */
	mapped_mb->mutex		= (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	mapped_mb->main_mb		= NULL;
	mapped_mb->dyn_bitmap	= NULL;

	return 1;
}
/**************************************************************************************************************************/
long MemBufferMappedGetSize(MemBufferMapped *mapped_mb)
{
	long total_size = 0;

	/* SUM up MAIN and PARTIAL sizes */
	total_size += MemBufferMappedMainGetSize(mapped_mb);
	total_size += MemBufferMappedPartialGetSize(mapped_mb);

	return total_size;
}
/**************************************************************************************************************************/
int MemBufferMappedPartialClean(MemBufferMapped *mapped_mb)
{
	DLinkedListNode *node;
	MemBuffer *cur_mb;
	int destroy_count;

	/* Sanity check */
	if (!mapped_mb)
		return 0;

	/* Point to partial list HEAD */
	node			= mapped_mb->partial_mb_list.head;
	destroy_count	= 0;

	/* Destroy all pending partial MEMBUFFERs */
	while (node)
	{
		cur_mb	= node->data;
		node	= node->next;

		/* Destroy and touch counter */
		MemBufferDestroy(cur_mb);
		destroy_count++;
		continue;
	}

	/* Reset partial data list */
	DLinkedListReset(&mapped_mb->partial_mb_list);

	//	/* Destroy all pending partial MEMBUFFERs */
	//	for (destroy_count = 0, node = mapped_mb->partial_mb_list.head; node; destroy_count++, node = node->next)
	//	{
	//		cur_mb = node->data;
	//		MemBufferDestroy(cur_mb);
	//		continue;
	//	}
	//
	//	/* Reset partial data list */
	//	DLinkedListInit(&mapped_mb->partial_mb_list, BRBDATA_THREAD_UNSAFE);

	return destroy_count;
}
/**************************************************************************************************************************/
long MemBufferMappedPartialGetSize(MemBufferMapped *mapped_mb)
{
	MemBuffer *page_part_mb;
	DLinkedListNode *node;

	long partial_sz = 0;

	/* Nothing on list, leave */
	if (DLINKED_LIST_ISEMPTY(mapped_mb->partial_mb_list))
		return 0;

	for (node = mapped_mb->partial_mb_list.head; node; node = node->next)
	{
		page_part_mb	= node->data;
		partial_sz		+= MemBufferGetSize(page_part_mb);

		continue;
	}

	return partial_sz;
}
/**************************************************************************************************************************/
long MemBufferMappedMainGetSize(MemBufferMapped *mapped_mb)
{
	/* Sanity check */
	if (!mapped_mb)
		return 0;

	return mapped_mb->main_mb->size;

}
/**************************************************************************************************************************/
int MemBufferMappedCompleted(MemBufferMapped *mapped_mb)
{
	MemBuffer *page_part_mb;
	DLinkedListNode *node;
	int begin_complete;

	/* Sanity check */
	if (!mapped_mb)
		return 0;

	/* Make sure we have all the START bytes */
	begin_complete = DynBitMapCheckMultiBlocks(mapped_mb->dyn_bitmap, 0, DynBitMapGetHigherSeen(mapped_mb->dyn_bitmap));

	/* Incomplete initial blocks */
	if (!begin_complete)
		return 0;

	/* There are partials floating, this object cannot be complete */
	if (mapped_mb->partial_mb_list.size > 1)
		return 0;

	//	/* Not more than one item left - Make sure its the last item */
	//	assert (mapped_mb->partial_mb_list.size <= 1);
	//
	//	/* There is more than one page flying around, and this should NEVER happen */
	//	if (mapped_mb->partial_mb_list.size > 1)
	//	{
	//		printf("MemBufferMappedCompleted - FATAL: HIGH [%d] - IN_LIST [%d]\n", DynBitMapGetHigherSeen(mapped_mb->dyn_bitmap), mapped_mb->partial_mb_list.size);
	//
	//		/* Dump all pages to help the coder make more sense of what kind of shit just happened */
	//		for (node = mapped_mb->partial_mb_list.head; node; node = node->next)
	//		{
	//			page_part_mb = node->data;
	//			printf("\tMemBufferMappedCompleted - FATAL: Remaining BLOCK_ID [%d] with [%d] bytes\n", page_part_mb->user_int, MemBufferGetSize(page_part_mb));
	//			continue;
	//		}
	//
	//		abort();
	//	}

	/* Append last partial item */
	if (mapped_mb->partial_mb_list.size)
	{
		/* Grab page and make sure its the LAST one */
		page_part_mb = mapped_mb->partial_mb_list.head->data;

		//printf("MemBufferMappedCompleted - FATAL: HIGH [%d] - BLOCK [%d]\n", DynBitMapGetHigherSeen(mapped_mb->dyn_bitmap), page_part_mb->user_int);
		//assert ((DynBitMapGetHigherSeen(mapped_mb->dyn_bitmap) + 1) == page_part_mb->user_int);

		/* Make sure its the last item */
		if ((DynBitMapGetHigherSeen(mapped_mb->dyn_bitmap) + 1) != page_part_mb->user_int)
			return 0;

		/* Set Last block as complete */
		DynBitMapBitSet(mapped_mb->dyn_bitmap, page_part_mb->user_int);

		/* Add to MAIN_MB and DROP incomplete page */
		MemBufferAdd(mapped_mb->main_mb, MemBufferDeref(page_part_mb), MemBufferGetSize(page_part_mb));
		MemBufferMappedPartialDrop(mapped_mb, page_part_mb);
	}

	/* Adjust MB size to minimum possible */
	MemBufferShrink(mapped_mb->main_mb);

	/* Mark mapped_mb as completed */
	mapped_mb->flags.complete			= 1;
	mapped_mb->flags.pct_string_update	= 1;

	return 1;
}
/**************************************************************************************************************************/
int MemBufferMappedAdd(MemBufferMapped *mapped_mb, char *data, long data_sz)
{
	/* Sanity check */
	if (!mapped_mb)
		return 0;

	/* Write and update internal offset */
	MemBufferMappedWrite(mapped_mb, mapped_mb->cur_offset, data_sz, data);
	mapped_mb->cur_offset += data_sz;

	return 1;
}
/**************************************************************************************************************************/
int MemBufferMappedWrite(MemBufferMapped *mapped_mb, long offset, long data_sz, char *data)
{
	char *rounded_data_ptr;
	long rounded_data_sz;
	long rounded_data_offset;
	long block_idx_lower;
	long block_idx_higher;
	long block_idx_begin;
	long block_idx_finish;
	long block_count_total;
	long block_size_trail;
	long block_size_finish;
	long block_off_relative;

	/* Sanity check */
	if (!mapped_mb)
		return 0;

	/* Calculate LOWER block INDEX, TOTAL block count and how many bytes are incomplete in trail data */
	block_idx_lower		= (offset / mapped_mb->page_sz);
	block_idx_higher	= ((offset + data_sz) / mapped_mb->page_sz);
	block_count_total	= (data_sz / mapped_mb->page_sz);
	block_off_relative	= (offset % mapped_mb->page_sz);

	/* Calculate MISALIGNED data on the TAIL and HEAD of BUFFER */
	block_size_trail	= ((block_off_relative > 0) ? (mapped_mb->page_sz - block_off_relative) : 0);
	block_size_trail	= ((block_size_trail > data_sz) ? data_sz : block_size_trail);
	block_size_finish	= ((data_sz - block_size_trail) % mapped_mb->page_sz);

	/* Calculate block INDEXEs where ALIGNED data BEGIN and FINISH */
	block_idx_begin		= ((block_size_trail > 0) ? (block_idx_lower + 1) : block_idx_lower);
	block_idx_finish	= ((block_size_finish > 0) ? (block_idx_higher + 1) : block_idx_higher);

	/* Calculate ROUNDED ALIGNED data begin PTR and SIZE */
	rounded_data_ptr	= (data + block_size_trail);
	rounded_data_sz		= (data_sz - (block_size_trail + block_size_finish));
	rounded_data_offset	= (block_idx_higher * mapped_mb->page_sz);

	//printf("MemBufferMappedWrite - ABS_OFFSET [%ld] - DATA_PAGE_SZ [%ld / %d] | BLOCK_LOW/BEGIN/HIGH/FINISH_IDX [%d / %d/ %d / %d] | BLOCK_COUNT [%d] - TRAIL/FINISH_DATA_SZ [%d / %d] - DATA -> [%d]-[%s]\n",
	//offset, data_sz, mapped_mb->page_sz, block_idx_lower, block_idx_begin, block_idx_higher, block_idx_finish,
	//block_count_total, block_size_trail, block_size_finish, rounded_data_sz, rounded_data_ptr);

	/* Dump the partial buffer */
	//EvKQBaseLogHexDump(data, data_sz, 8, 4);

	/* Issue the ALIGNED write invoke if there is DATA to write */
	if (rounded_data_sz > 0)
	{
		MemBufferMappedAlignedWrite(mapped_mb, rounded_data_ptr, rounded_data_sz, block_idx_begin);

		/* Update higher seen offset */
		if (mapped_mb->cur_offset < rounded_data_offset)
			mapped_mb->cur_offset = rounded_data_offset;
	}

	/* There is TRAIL data, save it */
	if (block_size_trail > 0)
		MemBufferMappedPartialDataProcess(mapped_mb, data, block_size_trail, block_idx_lower, block_off_relative);

	/* There is FINISH data, save it */
	if (block_size_finish > 0)
		MemBufferMappedPartialDataProcess(mapped_mb, (rounded_data_ptr + rounded_data_sz), block_size_finish, block_idx_higher, 0);

	/* Need PCT string UPDATE */
	mapped_mb->flags.pct_string_update	= 1;
	return 1;
}
/**************************************************************************************************************************/
int MemBufferMappedCheckBytes(MemBufferMapped *mapped_mb, long base, long offset)
{
	MemBuffer *page_part_mb;
	long base_byte_off;
	long offset_byte_off;
	long block_idx_begin;
	long block_idx_finish;
	long page_part_mb_sz;
	int have_bytes;

	/* Base MUST >= ZERO */
	if (base < 0)
		base = 0;

	/* If offset < Base, overwrite to size of MemBuffer MAIN  */
	if (offset < base)
		offset = MemBufferGetSize(mapped_mb->main_mb);

	/* Calculate indexes - CEIL the last block to upper next */
	block_idx_begin		= (base / mapped_mb->page_sz);
	block_idx_finish 	= (offset / mapped_mb->page_sz);

	/* Calculate OFFSETs - From inter-block offset */
	base_byte_off		= (base % mapped_mb->page_sz);
	offset_byte_off 	= (offset % mapped_mb->page_sz);

	//printf("MEM_CHECK - BASE: IDX [%d] OFF: [%ld] || OFF: IDX [%d] OFF: [%ld]\n",
	//block_idx_begin, base_byte_off, block_idx_finish, offset_byte_off);

	/* Base is partial request */
	if (base_byte_off > 0)
	{
		/* Walk our partial PAGE list and check if we have some partial data for this offset lying around */
		page_part_mb 	= MemBufferMappedPartialGetByIdx(mapped_mb, block_idx_begin);
		page_part_mb_sz = MemBufferGetSize(page_part_mb);

		/* We found this partial */
		if (page_part_mb)
		{
			/* Offset of partial is greater than base */
			if (base_byte_off < page_part_mb->user_long)
				return 0;

			/* Check if we are searching for the same block */
			if ((block_idx_begin == block_idx_finish))
			{
				/* We not satisfy the size to read offset */
				if (page_part_mb_sz < (offset_byte_off + 1))
					return 0;
				else
					return 1;
			}

			/* First partial, is broken */
			if (page_part_mb_sz < mapped_mb->page_sz)
				return 0;

			block_idx_begin++;
		}
	}


	/* Offset is partial request */
	if (offset_byte_off > 0)
	{
		/* Walk our partial PAGE list and check if we have some partial data for this offset lying around */
		page_part_mb 	= MemBufferMappedPartialGetByIdx(mapped_mb, block_idx_finish);
		page_part_mb_sz = MemBufferGetSize(page_part_mb);

		/* We found this partial */
		if (page_part_mb)
		{
			/* Partial don't have first bytes */
			if (page_part_mb->user_long > 0)
				return 0;

			/* We not satisfy the size to read offset */
			if (page_part_mb_sz < (offset_byte_off + 1))
				return 0;

			block_idx_finish--;
		}
	}

	/* Check if We have this blocks complete */
	have_bytes = DynBitMapCheckMultiBlocks(mapped_mb->dyn_bitmap, block_idx_begin, block_idx_finish);

	return have_bytes;
}
/**************************************************************************************************************************/
int MemBufferMappedGetValidBytes(MemBufferMapped *mapped_mb, long base, long offset, MemBufferMappedValidBytes *valid_bytes)
{
	DynBitMapValidBlockIdx valid_block_idx;
	MemBuffer *page_part_mb;
	long valid_blocks;
	long block_idx_begin;
	long block_idx_finish;

	long mapped_mb_sz		= MemBufferGetSize(mapped_mb->main_mb);
	long found_byte_base 	= 0;
	long found_byte_offset	= 0;
	long page_part_mb_sz 	= 0;

	/* Initialize */
	valid_bytes->first_byte = -1;
	valid_bytes->last_byte 	= -1;

	/* Sanity check values */
	if ((base > mapped_mb_sz) || (offset > mapped_mb_sz))
		return 0;

	/* Base MUST >= ZERO */
	if (base < 0)
		base = 0;

	/* If offset < Base, overwrite to size of MemBuffer MAIN  */
	if (offset < base)
		offset = MemBufferGetSize(mapped_mb->main_mb) - 1;

	/* Calculate indexes - CEIL the last block to upper next */
	block_idx_begin		= (base / mapped_mb->page_sz);
	block_idx_finish 	= (offset / mapped_mb->page_sz);

	//printf("MEM_CHECK - BASE: IDX [%d] OFF: [%ld] || OFF: IDX [%d] OFF: [%ld]\n", block_idx_begin, base_byte_off, block_idx_finish, offset_byte_off);

	/* Search which blocks are valid */
	valid_blocks 		= DynBitMapGetValidBlocks(mapped_mb->dyn_bitmap, block_idx_begin, block_idx_finish, &valid_block_idx);

	/* We have valid PAGE blocks that will satisfy this request */
	if (valid_blocks)
	{
		/* Calculate first and last valid bytes within the requested data chain */
		valid_bytes->first_byte = (valid_block_idx.first_idx * mapped_mb->page_sz);
		valid_bytes->last_byte 	= (valid_block_idx.last_idx * mapped_mb->page_sz) + (mapped_mb->page_sz - 1);

		//printf("01 - FIRST [%ld] - LAST [%ld]\n", valid_bytes->first_byte, valid_bytes->last_byte);

		/* We found base into complete blocks */
		if (valid_bytes->first_byte <= base)
		{
			valid_bytes->first_byte	= base;
			found_byte_base			= 1;
		}

		/* We found offset into complete blocks */
		if (valid_bytes->last_byte >= offset)
		{
			valid_bytes->last_byte	= offset;
			found_byte_offset		= 1;
		}

		//printf("02 - FIRST [%ld] - LAST [%ld]\n", valid_bytes->first_byte, valid_bytes->last_byte);

		/* Grab current valid blocks INDEXES */
		block_idx_begin 	= (valid_block_idx.first_idx - 1);
		block_idx_finish 	= (valid_block_idx.last_idx + 1);

	}
	/* There is not enough blocks to satisfy this request */
	else
		block_idx_finish	= block_idx_begin;

	/* Check for first bytes into partial */
	if (!found_byte_base)
	{
		/* Grab partial */
		page_part_mb 	= MemBufferMappedPartialGetByIdx(mapped_mb, block_idx_begin);
		page_part_mb_sz = MemBufferGetSize(page_part_mb);

		/* We found a partial */
		if (page_part_mb)
		{
			/* Base of partial is greater than base - Sanity check last byte information */
			valid_bytes->first_byte = (block_idx_begin * mapped_mb->page_sz) + page_part_mb->user_long;
			valid_bytes->first_byte	= (valid_bytes->first_byte < base) ? -1 : valid_bytes->first_byte;

			/* Check if we are searching for the same block */
			if ((block_idx_begin == block_idx_finish))
			{
				valid_bytes->last_byte 	= (block_idx_begin * mapped_mb->page_sz) + (page_part_mb_sz - 1);
				found_byte_offset 		= 1;
			}
		}
	}

	//printf("03 - FIRST [%ld] - LAST [%ld]\n", valid_bytes->first_byte, valid_bytes->last_byte);

	/* Check for last bytes into partial */
	if (!found_byte_offset)
	{
		/* Walk our partial PAGE list and check if we have some partial data for this offset lying around */
		page_part_mb 	= MemBufferMappedPartialGetByIdx(mapped_mb, block_idx_finish);
		page_part_mb_sz = MemBufferGetSize(page_part_mb);

		/* We found this partial */
		if (page_part_mb)
		{
			/* Partial don't have first bytes */
			if (page_part_mb->user_long == 0)
				valid_bytes->last_byte 	= (block_idx_finish * mapped_mb->page_sz) + (page_part_mb_sz - 1);
		}
	}

	/* Correct last byte info */
	valid_bytes->last_byte	= (valid_bytes->last_byte < base) ? -1 : valid_bytes->last_byte;

	//printf("04 - FIRST [%ld] - LAST [%ld]\n", valid_bytes->first_byte, valid_bytes->last_byte);

	/* Give up, we have nothing */
	if ((valid_bytes->first_byte < 0) || valid_bytes->last_byte < 0)
		return 0;

	/* Populate absolute valid range information */
	valid_bytes->first_byte 	= (valid_bytes->first_byte < base) ? base : valid_bytes->first_byte;
	valid_bytes->last_byte 		= (valid_bytes->last_byte > offset) ? offset : valid_bytes->last_byte;

	return 1;
}
/**************************************************************************************************************************/
int MemBufferMappedCheckLastBlock(MemBufferMapped *mapped_mb, long total_sz)
{
	/* Grab Partial by block index */
	long last_block_sz			= (total_sz % mapped_mb->page_sz);
	long last_page_idx			= (total_sz / mapped_mb->page_sz);
	MemBuffer *page_part_mb 			= MemBufferMappedPartialGetByIdx(mapped_mb, last_page_idx);
	long  page_part_mb_sz		= MemBufferGetSize(page_part_mb);

	/* Have last part, check is its complete */
	if (page_part_mb)
	{
		//printf("CHECK LAST BLOCK [%d] - PAGE_SZ [%d] - PAGE_OFF [%ld] - SIZES [%ld] -> [%ld]\n",
		//		last_page_idx, mapped_mb->page_sz, page_part_mb->user_long, page_part_mb_sz, last_block_sz);

		/* Partial don't have first bytes */
		if (page_part_mb->user_long > 0)
			return 0;

		if (page_part_mb_sz >= last_block_sz)
		{
			/* Set Last block as complete */
			DynBitMapBitSet(mapped_mb->dyn_bitmap, last_page_idx);

			/* Write main aligned data */
			MemBufferOffsetWrite(mapped_mb->main_mb, (total_sz - last_block_sz), MemBufferDeref(page_part_mb), last_block_sz);
			MemBufferMappedPartialDrop(mapped_mb, page_part_mb);
		}
	}

	return 1;
}
/**************************************************************************************************************************/
float MemBufferMappedCalculateProgressPCT(MemBufferMapped *mapped_mb, long total_sz)
{
	int page_count;
	long i;

	/* Sanity check */
	if (!mapped_mb)
		return 0;

	DynBitMap *dyn_bitmap  		= mapped_mb->dyn_bitmap;
	int last_page_idx			= (total_sz / mapped_mb->page_sz);

	/* Nothing to calculate, bail out */
	if (dyn_bitmap->bitmap.higher_seen <= 0)
		mapped_mb->progress_pct = 0.0;

	/* Walk all PAGEs */
	for (i = 0, page_count = 0; i < last_page_idx; i++)
	{
		page_count += DynBitMapBitTest(dyn_bitmap, i);
		continue;
	}

	/* Calculate PROGRESS PCT */
	mapped_mb->progress_pct = (page_count > 0) ? ((100 * page_count) / last_page_idx) : 0.0;

	return mapped_mb->progress_pct;
}
/**************************************************************************************************************************/
int MemBufferMappedGenerateStringPCT(MemBufferMapped *mapped_mb, char *ret_mask, int ret_mask_size, long total_sz)
{
	int active_factor;
	int active_factor_mod;
	int bit_active;
	long i;
	long j;

	/* Sanity check */
	if (!mapped_mb)
		return 0;

	/* Already update, bail out */
	if (!mapped_mb->flags.pct_string_update)
		return 0;

	DynBitMap *dyn_bitmap  		= mapped_mb->dyn_bitmap;
	int last_page_idx			= (total_sz / mapped_mb->page_sz);
	int cc						= 0;
	int ci						= 0;
	int step_idx 				= 0;
	int step_offset 			= 0;

	/* Sanity check */
	if (!dyn_bitmap || dyn_bitmap->bitmap.higher_seen <= 0)
		return 0;

	/* Calculate how many continuous blocks will set a single mask byte */
	active_factor 		= (last_page_idx < ret_mask_size) ? 1 : (last_page_idx / ret_mask_size);
	active_factor_mod	= (last_page_idx % ret_mask_size);

	/* Walk all DOTs */
	for (i = 0; i < last_page_idx; i++)
	{
		bit_active	= DynBitMapBitTest(dyn_bitmap, i);
		cc			+= bit_active ? 1 : 0;

		if (((i % active_factor) == 0) || (i == last_page_idx))
		{
			step_offset = ((i + 1) * ret_mask_size) / last_page_idx;

			/* Print in chars the progress */
			for (ci = step_idx; ci < step_offset, ci < ret_mask_size; ci++)
				ret_mask[ci]	= cc > 0 ? '1' : '0';

			cc 			= 0;
			step_idx	= step_offset;

			continue;
		}

		continue;
	}

	/* Do not need update */
	mapped_mb->flags.pct_string_update	= 0;
	return 1;
}
/**************************************************************************************************************************/
MemBuffer *MemBufferMappedPartialGetByIdx(MemBufferMapped *mapped_mb, long block_idx)
{
	MemBuffer *page_part_mb;
	DLinkedListNode *node;

	/* Walk all partial blocks searching for desired BLOCK */
	for (node = mapped_mb->partial_mb_list.head; node; node = node->next)
	{
		/* Grab Partial MB */
		page_part_mb 	= node->data;

		/* Block found, STOP */
		if (block_idx == page_part_mb->user_int)
			return page_part_mb;

		continue;
	}

	return NULL;
}
/**************************************************************************************************************************/
int MemBufferMappedMetaDataPack(MemBufferMapped *mapped_mb, MetaData *dst_metadata, long item_sub_id)
{
	/* Sanity check */
	if (!mapped_mb)
		return 0;

	/* Clean up stack */
	memset(&mapped_mb->metadata, 0, sizeof(MemBufferMappedMetaData));

	/* Populate METADATA */
	mapped_mb->metadata.cur_offset	= mapped_mb->cur_offset;
	mapped_mb->metadata.page_sz		= mapped_mb->page_sz;
	memcpy(&mapped_mb->metadata.flags, &mapped_mb->flags, sizeof(MemBufferMappedFlags));

//	printf("MemBufferMappedMetaDataPack - DATATYPE_MEMBUFFER_MAPPED_META with [%d] bytes - CUR_OFF [%ld] - PAGE_SZ [%d]\n",
//			sizeof(MemBufferMappedMetaData), mapped_mb->metadata.cur_offset, mapped_mb->metadata.page_sz);

	/* Dump MEMBUFFER METADATA and BITMAP METADATA to COMPLETE METADATA */
	MetaDataItemAdd(dst_metadata, DATATYPE_MEMBUFFER_MAPPED_META, item_sub_id, &mapped_mb->metadata, sizeof(MemBufferMappedMetaData));
	DynBitMapMetaDataPack(mapped_mb->dyn_bitmap, dst_metadata, item_sub_id);

	/* Dump RAW DATA */
	MetaDataItemAdd(dst_metadata, DATATYPE_MEMBUFFER_MAPPED_DATA, item_sub_id, MemBufferDeref(mapped_mb->main_mb), MemBufferGetSize(mapped_mb->main_mb));

	return 1;
}
/**************************************************************************************************************************/
int MemBufferMappedMetaDataUnPack(MemBufferMapped *mapped_mb, MemBuffer *raw_metadata_mb)
{
	MemBufferMappedMetaData *mb_mapped_meta;
	DynBitMapMetaData *dyn_bitmap_meta;
	MetaDataItem *meta_item;
	MetaData metadata;
	int op_status;
	int i;

	/* If there is a DYN_BITMAP already initialized, destroy it */
	if (mapped_mb->dyn_bitmap)
		DynBitMapDestroy(mapped_mb->dyn_bitmap);

	/* Begin by unpacking DYN_BITMAP information from this RAW_MB */
	mapped_mb->dyn_bitmap = DynBitMapMetaDataUnPackFromMB(raw_metadata_mb);

	/* Failed unpacking BITMAP, stop */
	if (!mapped_mb->dyn_bitmap)
	{
//		printf("MemBufferMappedMetaDataUnPack - Failed unpacking MB_MAPPED\n");
		return 0;
	}

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

		/* MB_MAPPED METADATA, populate back */
		if (DATATYPE_MEMBUFFER_MAPPED_META == meta_item->item_id)
		{
			mb_mapped_meta = meta_item->ptr;

			mapped_mb->cur_offset	= mb_mapped_meta->cur_offset;
			mapped_mb->page_sz		= mb_mapped_meta->page_sz;
			memcpy(&mapped_mb->flags, &mb_mapped_meta->flags, sizeof(MemBufferMappedFlags));

//			printf("MemBufferMappedMetaDataUnPack - DATATYPE_MEMBUFFER_MAPPED_META with [%d] bytes - CUR_OFF [%ld] - PAGE_SZ [%d]\n",
//					meta_item->sz, mb_mapped_meta->cur_offset, mb_mapped_meta->page_sz);

			continue;
		}

		/* MB_MAPPED RAW_DATA, copy */
		else if (DATATYPE_MEMBUFFER_MAPPED_DATA == meta_item->item_id)
		{
			/* Reset MAIN_MB */
			MemBufferDestroy(mapped_mb->main_mb);
			mapped_mb->main_mb = MemBufferNew(BRBDATA_THREAD_UNSAFE, 1024);

			/* Load RAW data BACK */
			MemBufferAdd(mapped_mb->main_mb, meta_item->ptr, meta_item->sz);
//			printf("MemBufferMappedMetaDataUnPack - DATATYPE_MEMBUFFER_MAPPED_DATA with [%lu] bytes\n", meta_item->sz);

			continue;
		}

		continue;
	}

	/* Set flags to allow STRING_PCT calculation once we are done */
	mapped_mb->flags.pct_string_update = 1;

	/* Clean UP METDATA and leave */
	MetaDataClean(&metadata);
	return 1;
}
/**************************************************************************************************************************/
MemBuffer *MemBufferMappedMetaDataPackToMB(MemBufferMapped *mapped_mb, long item_sub_id)
{
	MetaData metadata;
	MemBuffer *packed_mb = MemBufferNew(BRBDATA_THREAD_UNSAFE, 1024);

	/* Clean up stack */
	memset(&metadata, 0, sizeof(MetaData));

	/* Pack data into METADATA SERIALIZER and DUMP it into MEMBUFFER */
	MemBufferMappedMetaDataPack(mapped_mb, &metadata, item_sub_id);
	MetaDataPack(&metadata, packed_mb);

	/* Clean UP METDATA and leave */
	MetaDataClean(&metadata);

	return packed_mb;
}
/**************************************************************************************************************************/
MemBufferMapped *MemBufferMappedMetaDataUnPackFromMeta(MetaData *metadata)
{
	MemBufferMappedMetaData *mb_mapped_meta;
	MemBufferMapped *mapped_mb;
	MetaDataItem *meta_item;
	int op_status;
	int i;

	/* Create a new mapped_MB and unpack - Page size will be replaced by correct FILE-STORED PAGE_SZ of previous MB_MAPPED */
	mapped_mb	= MemBufferMappedNew(BRBDATA_THREAD_UNSAFE, 128);

	/* If there is a DYN_BITMAP already initialized, destroy it */
	if (mapped_mb->dyn_bitmap)
		DynBitMapDestroy(mapped_mb->dyn_bitmap);

	/* Unpack BITMAP back from META */
	mapped_mb->dyn_bitmap = DynBitMapMetaDataUnPackFromMeta(metadata);

	/* Now walk all items */
	for (i = metadata->item_offset; i < metadata->items.count; i++)
	{
		meta_item = MemArenaGrabByID(metadata->items.arena, i);

		/* MB_MAPPED METADATA, populate back */
		if (DATATYPE_MEMBUFFER_MAPPED_META == meta_item->item_id)
		{
			mb_mapped_meta = meta_item->ptr;

			mapped_mb->cur_offset	= mb_mapped_meta->cur_offset;
			mapped_mb->page_sz		= mb_mapped_meta->page_sz;
			memcpy(&mapped_mb->flags, &mb_mapped_meta->flags, sizeof(MemBufferMappedFlags));

//			printf("MemBufferMappedMetaDataUnPackFromMeta - DATATYPE_MEMBUFFER_MAPPED_META with [%d] bytes - CUR_OFF [%ld] - PAGE_SZ [%d]\n",
//					meta_item->sz, mb_mapped_meta->cur_offset, mb_mapped_meta->page_sz);

			continue;
		}

		/* MB_MAPPED RAW_DATA, copy */
		else if (DATATYPE_MEMBUFFER_MAPPED_DATA == meta_item->item_id)
		{
			/* Reset MAIN_MB */
			MemBufferDestroy(mapped_mb->main_mb);
			mapped_mb->main_mb = MemBufferNew(BRBDATA_THREAD_UNSAFE, 1024);

			/* Load RAW data BACK */
			MemBufferAdd(mapped_mb->main_mb, meta_item->ptr, meta_item->sz);
//			printf("MemBufferMappedMetaDataUnPackFromMeta - DATATYPE_MEMBUFFER_MAPPED_DATA with [%lu] bytes\n", meta_item->sz);

			continue;
		}

		continue;
	}

	/* Set flags to allow STRING_PCT calculation once we are done */
	mapped_mb->flags.pct_string_update = 1;

	return mapped_mb;
}
/**************************************************************************************************************************/
MemBufferMapped *MemBufferMappedMetaDataUnPackFromMB(MemBuffer *raw_metadata_mb)
{
	MemBufferMapped *mapped_mb;
	int op_status;

	/* Create a new mapped_MB and unpack - Page size will be replaced by correct FILE-STORED PAGE_SZ of previous MB_MAPPED */
	mapped_mb	= MemBufferMappedNew(BRBDATA_THREAD_UNSAFE, 128);
	op_status	= MemBufferMappedMetaDataUnPack(mapped_mb, raw_metadata_mb);

	/* Failed unpacking METADATA, destroy and LEAVE */
	if (op_status <= 0)
	{
		printf("MemBufferMappedMetaDataUnPackFromMB - Failed unpacking MAPPED_MB\n");
		MemBufferMappedDestroy(mapped_mb);
		return NULL;
	}

	return mapped_mb;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int MemBufferMappedAlignedWrite(MemBufferMapped *mapped_mb, char *data, long data_sz, long block_idx)
{
	MemBuffer *page_part_mb;
	int i;

	long abs_offset			= (block_idx * mapped_mb->page_sz);
	long block_count		= (data_sz / mapped_mb->page_sz);
	long block_upper_idx	= block_count + block_idx;

	/* Write must be ALIGNED to PAGE_SIZE */
	assert((data_sz % mapped_mb->page_sz) == 0);

	/* Write main aligned data */
	MemBufferOffsetWrite(mapped_mb->main_mb, abs_offset, data, data_sz);

	//	printf("MemBufferMappedAlignedWrite - Writing [%d] bytes from BLOCK_IDX [%d] with ABSOLUTE offset [%ld]\n", data_sz, block_idx, abs_offset);

	/* Update bitmap */
	for (i = block_idx; i < block_upper_idx; i++)
	{
		DynBitMapBitSet(mapped_mb->dyn_bitmap, i);

		/* Check if there is a partial to this block */
		page_part_mb = MemBufferMappedPartialGetByIdx(mapped_mb, i);

		/* Remove Partial */
		if (page_part_mb)
			MemBufferMappedPartialDrop(mapped_mb, page_part_mb);

		continue;
	}

	return 1;
}
/**************************************************************************************************************************/
static int MemBufferMappedPartialDataProcess(MemBufferMapped *mapped_mb, char *data, long data_sz, long block_idx, long rel_offset)
{
	MemBuffer *page_part_mb;
	long abs_offset			= (block_idx * mapped_mb->page_sz);
	long partial_mb_off 	= 0;
	long page_part_new_sz 	= 0;
	long page_part_mb_off 	= 0;
	long page_part_mb_sz	= 0;
	long block_exists		= DynBitMapBitTest(mapped_mb->dyn_bitmap, block_idx);

	/* Block already complete, do not try to write */
	if (block_exists)
	{
		//printf("MemBufferMappedPartialDataProcess - BLOCK [%d] - Already COMPLETE\n", block_idx);
		return 0;
	}

	//	//printf("MemBufferMappedTrailDataProcess - BLOCK [%d] - DATA_SZ [%d] - REL_OFFSET [%d]\n", block_idx, data_sz, rel_offset);

	/* Grab Partial by block index */
	page_part_mb = MemBufferMappedPartialGetByIdx(mapped_mb, block_idx);

	if (page_part_mb)
	{
		page_part_mb_sz 	= MemBufferGetSize(page_part_mb);
		page_part_mb_off	= (page_part_mb->user_long + page_part_mb_sz);
		page_part_new_sz	= (data_sz + rel_offset);

		/* This is a append */
		if (rel_offset < page_part_mb->user_long)
		{
			/* Return, this data not meet the block */
			if (page_part_new_sz < page_part_mb->user_long)
				return 0;
		}

		/* Off set must be equal the partial data off */
		if (rel_offset > page_part_mb_off)
			return 0;

		/* This is a prepend */
		if (rel_offset > page_part_mb->user_long)
		{
			/* This is a overwrite, skip out, we already have this bytes */
			if (page_part_new_sz < page_part_mb_off)
				return 0;
		}

		/* Grab new offset */
		partial_mb_off = (rel_offset > page_part_mb->user_long ? page_part_mb->user_long : rel_offset);
	}
	else
	{
		/* Create a new buffer the size of a page, and stuff data into it */
		page_part_mb = MemBufferNew(BRBDATA_THREAD_UNSAFE, mapped_mb->page_sz);

		/* Add trail MB to partial list */
		DLinkedListAdd(&mapped_mb->partial_mb_list, &page_part_mb->node, page_part_mb);
		partial_mb_off = rel_offset;
	}

	/* Save block index and relative offset - NEGATIVE FOR TRAIL, POSITIVE FOR FINISH */
	page_part_mb->user_long	= partial_mb_off;
	page_part_mb->user_int	= block_idx;

	/* Stuff data into buffer of a page */
	MemBufferOffsetWrite(page_part_mb, rel_offset, data, data_sz);

	/* Write main aligned data */
	MemBufferOffsetWrite(mapped_mb->main_mb, (abs_offset + rel_offset), data, data_sz);
	page_part_mb_sz = MemBufferGetSize(page_part_mb);

	/* Page has complete */
	if ((page_part_mb_sz - partial_mb_off) >= mapped_mb->page_sz)
	{
		/* Fill it into main, remove MB from partial_list and destroy it */
		MemBufferMappedAlignedWrite(mapped_mb, MemBufferDeref(page_part_mb), mapped_mb->page_sz, block_idx);

		/* Replaced by this code that try to find this page inside LIST, and drop it if it was not DROPED by ALIGEND WRITE above */
		page_part_mb = MemBufferMappedPartialGetByIdx(mapped_mb, block_idx);

		/* We are still alive, DROP */
		if (page_part_mb)
			MemBufferMappedPartialDrop(mapped_mb, page_part_mb);
	}

	return 1;
}
/**************************************************************************************************************************/
static int MemBufferMappedPartialDrop(MemBufferMapped *mapped_mb, MemBuffer *page_part_mb)
{
	/* Remove MB from List and destroy it */
	DLinkedListDelete(&mapped_mb->partial_mb_list, &page_part_mb->node);
	MemBufferDestroy(page_part_mb);

	return 1;
}
/**************************************************************************************************************************/














