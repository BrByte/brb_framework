/*
 * meta_data.c
 *
 *  Created on: 2014-11-02
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

static int MetaDataHeaderLoadData(MetaData *meta_data, MetaDataHeader *meta_data_hdr);

/**************************************************************************************************************************/
MetaData *MetaDataNew(void)
{
	MetaData *meta_data;

	meta_data = calloc(1, sizeof(MetaData));
	MetaDataInit(meta_data);

	return meta_data;
}
/**************************************************************************************************************************/
int MetaDataInit(MetaData *meta_data)
{
	/* Already initialized, bail out */
	if (meta_data->flags.initialized)
		return 0;

	/* Create a new arena for DATA_ITEMs */
	meta_data->items.arena			= MemArenaNew(4, (sizeof(MetaDataItem) + 1), 8, BRBDATA_THREAD_UNSAFE);
	meta_data->flags.initialized	= 1;

	return 1;
}
/**************************************************************************************************************************/
int MetaDataDestroy(MetaData *meta_data)
{
	/* Sanity check */
	if (!meta_data)
		return 0;

	/* Clean up and destroy */
	MetaDataClean(meta_data);
	free(meta_data);
	return 1;
}
/**************************************************************************************************************************/
int MetaDataReset(MetaData *meta_data)
{
	/* Sanity check */
	if (!meta_data)
		return 0;

	meta_data->items.count	= 0;
	meta_data->raw_data		= NULL;

	return 1;
}
/**************************************************************************************************************************/
int MetaDataClean(MetaData *meta_data)
{
	/* Sanity check */
	if (!meta_data)
		return 0;

	/* Destroy MB */
	MemArenaDestroy(meta_data->items.arena);

	/* Reset DATA */
	meta_data->items.arena			= NULL;
	meta_data->raw_data				= NULL;
	meta_data->flags.initialized	= 0;
	return 1;
}
/**************************************************************************************************************************/
int MetaDataPack(MetaData *meta_data, MemBuffer *meta_data_mb)
{
	MetaDataHeader meta_data_hdr;
	MetaDataItem *meta_item;
	int i;

	/* Sanity check */
	if (!meta_data)
		return 0;

	/* Clean up stack and initialize METADATA HEADER and WRITE it into PACKED_METADATA */
	memset(&meta_data_hdr, 0, sizeof(MetaDataHeader));
	MetaDataHeaderLoadData(meta_data, &meta_data_hdr);
	MemBufferAdd(meta_data_mb, &meta_data_hdr, sizeof(MetaDataHeader));

	/* Now serialize all binary items into MB */
	for (i = 0; i < meta_data->items.count; i++)
	{
		meta_item = MemArenaGrabByID(meta_data->items.arena, i);

		/* Add ITEM_ID and SIZE */
		MemBufferAdd(meta_data_mb, &meta_item->item_id, sizeof(long));
		MemBufferAdd(meta_data_mb, &meta_item->item_sub_id, sizeof(long));
		MemBufferAdd(meta_data_mb, &meta_item->sz, sizeof(long));

		/* Now add DATA */
		MemBufferAdd(meta_data_mb, meta_item->ptr, meta_item->sz);

		/* Now the CANARY */
		MemBufferAdd(meta_data_mb, METADATA_UNIT_SEPARATOR, METADATA_UNIT_SEPARATOR_SZ);
		continue;
	}

	//printf("MetaDataPack - Packed [%d] items - MB at [%p] with [%d / %d] bytes\n", i, meta_data_mb,
	//		(meta_data_hdr.size + sizeof(MetaDataHeader)), MemBufferGetSize(meta_data_mb));

	//EvKQBaseLogHexDump(MemBufferDeref(meta_data_mb), MemBufferGetSize(meta_data_mb), 4, 8);

	return 1;
}
/**************************************************************************************************************************/
int MetaDataUnpack(MetaData *meta_data, MemBuffer *meta_data_mb, MetaDataUnpackerInfo *unpacker_info)
{
	MetaDataHeader *meta_data_hdr;
	MetaDataItem *meta_item;
	BRB_MD5_CTX md5_digest;
	char *raw_data_ptr;
	char *canary_ptr;
	int error_code;

	unsigned long cur_remaining		= 0;
	unsigned long cur_needed		= 0;
	unsigned long cur_item_count	= 0;
	unsigned long old_mb_offset		= meta_data_mb ? MemBufferOffsetGet(meta_data_mb) : 0;
	unsigned long cur_offset		= old_mb_offset;

	/* Sanity check */
	if (!meta_data)
	{
		error_code = METADATA_UNPACK_FAILED_UNINITIALIZED;
		goto error;
	}

	//EvKQBaseLogHexDump(MemBufferDeref(meta_data_mb), 512, 4, 8);

	/* Clean up stack */
	memset(&md5_digest, 0, sizeof(BRB_MD5_CTX));

	/* Initialize md5 contexts */
	BRB_MD5Init(&md5_digest);

	/* Save reference of RAW_DATA and point to METADATA_HEADER */
	meta_data->raw_data	= meta_data_mb;
	meta_data_hdr		= MemBufferDeref(meta_data->raw_data);

	/* METADATA string DO NOT MATCH, bail out */
	if (memcmp(&meta_data_hdr->str, METADATA_MAGIC_HEADER, METADATA_MAGIC_HEADER_SZ))
	{
		//printf("MetaDataUnpack - Invalid HEADER_MAGIC\n");

		meta_data_mb->offset -= 64;

		//EvKQBaseLogHexDump(MemBufferDeref(meta_data_mb), 512, 4, 8);

		/* Set error and leave */
		error_code = METADATA_UNPACK_FAILED_INVALID_HEADER_MAGIC;
		goto error;
	}

	/* Set offset to bypass HEADER */
	MemBufferOffsetIncrement(meta_data->raw_data, (sizeof(MetaDataHeader)));
	cur_offset += sizeof(MetaDataHeader);

	/* Unpack ITEMs */
	while (1)
	{
		/* Item count requested by upper layers reached, STOP */
		if (unpacker_info && (cur_item_count >= unpacker_info->control.max_item_count))
		{
			//printf("MetaDataUnpack - Item count reached - CUR [%lu] - WANTED [%lu] \n", cur_item_count, unpacker_info->control.max_item_count);
			break;
		}

		/* Get current remaining bytes of RAW_MEM_BUFFER */
		cur_remaining = MemBufferGetSize(meta_data->raw_data);

		/* Remaining data is not enough to read a meta data item. Data is either MISSING or CORRUPTED */
		if (cur_remaining < sizeof(MetaDataItem))
		{
			//printf("MetaDataUnpack - Need more data to read META_ITEM\n");

			/* Calculate needed bytes, set error and leave */
			cur_needed	= (sizeof(MetaDataItem) - cur_remaining);
			error_code	= METADATA_UNPACK_FAILED_NEED_MORE_DATA_METAITEM;
			goto error;
		}

		/* Point to META_ITEM and adjust OFFSET to DATA */
		meta_item	= MemBufferDeref(meta_data->raw_data);
		MemBufferOffsetIncrement(meta_data->raw_data, METADATA_ITEM_RAW_SZ);

		/* Recalculate CUR_OFFSET and CUR_REMAINING data */
		cur_offset		+= METADATA_ITEM_RAW_SZ;
		cur_remaining	-= METADATA_ITEM_RAW_SZ;

		//printf("MetaDataUnpack - Will UNPACK ITEM/SUBITEM ID [%lu / %lu] - with [%lu] BYTES\n", meta_item->item_id, meta_item->item_sub_id, meta_item->sz);

		/* Not enough in the buffer to read packed object - Need more data */
		if (cur_remaining < (meta_item->sz + METADATA_UNIT_SEPARATOR_SZ))
		{
			/* Calculate needed bytes, set error and leave */
			cur_needed	= ((meta_item->sz + METADATA_UNIT_SEPARATOR_SZ) - cur_remaining);
			error_code	= METADATA_UNPACK_FAILED_NEED_MORE_DATA_OBJECT;

			//printf("MetaDataUnpack - Need more data to read DATA_OBJECT - REMAIN [%d] - NEED [%d] - SIZE [%d]\n", cur_remaining, cur_needed, meta_item->sz);

			goto error;
		}

		/* Add item into META_DATA */
		raw_data_ptr = MemBufferDeref(meta_data->raw_data);
		MetaDataItemAdd(meta_data, meta_item->item_id, meta_item->item_sub_id, raw_data_ptr, meta_item->sz);

		/* DIGEST DATA and adjust OFFSET to CANARY */
		BRB_MD5UpdateBig(&md5_digest, raw_data_ptr, meta_item->sz);
		MemBufferOffsetIncrement(meta_data->raw_data, meta_item->sz);

		/* Recalculate CUR_OFFSET and CUR_REMAINING data */
		cur_offset		+= meta_item->sz;
		cur_remaining	-= meta_item->sz;

		/* Now the CANARY */
		canary_ptr	= MemBufferDeref(meta_data->raw_data);

		/* Check canary */
		if (memcmp(canary_ptr, METADATA_UNIT_SEPARATOR, METADATA_UNIT_SEPARATOR_SZ))
		{
			//printf("MetaDataUnpack - Corrupted CANARY\n");

			/* Set error and leave */
			error_code = METADATA_UNPACK_FAILED_CORRUPTED_CANARY;
			goto error;
		}

		/* Point to next META_ITEM */
		MemBufferOffsetIncrement(meta_data->raw_data, METADATA_UNIT_SEPARATOR_SZ);

		/* Recalculate CUR_OFFSET and CUR_REMAINING data */
		cur_offset		+= METADATA_UNIT_SEPARATOR_SZ;
		cur_remaining	-= METADATA_UNIT_SEPARATOR_SZ;
		cur_item_count++;

		/* Finished buffer, STOP */
		if (MemBufferOffsetGet(meta_data->raw_data) == meta_data->raw_data->size)
			break;

		continue;
	}

	/* Finalize MD5 DIGEST */
	BRB_MD5Final(&md5_digest);

	/* MD5 digest INVALID, shout */
	if (memcmp(&md5_digest.digest, &meta_data_hdr->digest, sizeof(meta_data_hdr->digest)))
	{
		//printf("MetaDataUnpack - Corrupted MD5 DIGEST - Current [%s] - Expected [%s]\n", md5_digest.string, BrbHexToStrStatic((char*)&meta_data_hdr->digest, 16));

		/* Set error and leave */
		error_code = METADATA_UNPACK_FAILED_DIGEST_INVALID;
		goto error;
	}

	//printf("MetaDataUnpack - Unpacked [%d] items with valid DIGEST [%s]\n", meta_data->items.count, BrbHexToStrStatic((char*)&meta_data_hdr->digest, sizeof(meta_data_hdr->digest)));

	/* Fill in UNPACKER_INFO information */
	if (unpacker_info)
	{
		unpacker_info->error_code		= METADATA_UNPACK_SUCCESS;
		unpacker_info->cur_offset		= cur_offset;
		unpacker_info->cur_remaining	= cur_remaining;
		unpacker_info->cur_needed		= cur_needed;
	}

	/* Reset OFFSET and leave */
	MemBufferOffsetSet(meta_data->raw_data, old_mb_offset);
	return METADATA_UNPACK_SUCCESS;

	/* TAG to handler ERRORs and INCOMPLETE data */
	error:

	/* Fill in UNPACKER_INFO information */
	if (unpacker_info)
	{
		unpacker_info->error_code		= error_code;
		unpacker_info->cur_offset		= cur_offset;
		unpacker_info->cur_remaining	= cur_remaining;
		unpacker_info->cur_needed		= cur_needed;
	}

	/* Reset OFFSET and leave */
	MemBufferOffsetSet(meta_data->raw_data, old_mb_offset);
	return error_code;
}
/**************************************************************************************************************************/
int MetaDataItemAdd(MetaData *meta_data, unsigned long item_id, unsigned long item_sub_id, void *data_ptr, unsigned long data_sz)
{
	MetaDataItem *meta_item;

	/* Sanity check */
	if (!meta_data)
		return 0;

	/* Initialize if not initialized */
	MetaDataInit(meta_data);

	/* Grab meta item */
	meta_item = MemArenaGrabByID(meta_data->items.arena, meta_data->items.count++);

	/* Populate ITEM data */
	meta_item->item_id		= item_id;
	meta_item->item_sub_id	= item_sub_id;
	meta_item->ptr			= data_ptr;
	meta_item->sz			= data_sz;

	return 1;
}
/**************************************************************************************************************************/
MetaDataItem *MetaDataItemGrabMetaByID(MetaData *meta_data, unsigned long item_id, unsigned long item_sub_id)
{
	MetaDataItem *meta_item;
	int i;

	/* Now walk all items */
	for (i = meta_data->item_offset; i < meta_data->items.count; i++)
	{
		meta_item = MemArenaGrabByID(meta_data->items.arena, i);

		/* DYN_BITMAP METADATA, populate back */
		if ((item_id == meta_item->item_id) && (item_sub_id == meta_item->item_sub_id))
			return meta_item;

		continue;
	}

	return NULL;
}
/**************************************************************************************************************************/
void *MetaDataItemFindByID(MetaData *meta_data, unsigned long item_id, unsigned long item_sub_id)
{
	MetaDataItem *meta_item;
	int i;

	/* Now walk all items */
	for (i = meta_data->item_offset; i < meta_data->items.count; i++)
	{
		meta_item = MemArenaGrabByID(meta_data->items.arena, i);

		/* DYN_BITMAP METADATA, populate back */
		if ((item_id == meta_item->item_id) && (item_sub_id == meta_item->item_sub_id))
			return meta_item->ptr;

		continue;
	}

	return NULL;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int MetaDataHeaderLoadData(MetaData *meta_data, MetaDataHeader *meta_data_hdr)
{
	BRB_MD5_CTX md5_digest;
	MetaDataItem *meta_item;
	int i;

	/* Clean up stack */
	memset(&md5_digest, 0, sizeof(BRB_MD5_CTX));

	/* Initialize md5 contexts */
	BRB_MD5Init(&md5_digest);

	/* Populate META_HEADER */
	meta_data_hdr->version		= 0;
	meta_data_hdr->item_count	= meta_data->items.count;
	memcpy(&meta_data_hdr->str, METADATA_MAGIC_HEADER, METADATA_MAGIC_HEADER_SZ);

	/* Now serialize all binary items into MB */
	for (i = 0; i < meta_data->items.count; i++)
	{
		meta_item = MemArenaGrabByID(meta_data->items.arena, i);

		/* Digest DATA */
		BRB_MD5UpdateBig(&md5_digest, meta_item->ptr, meta_item->sz);

		/* DATA_SZ + (Item ID + Item SIZE) + CANARY */
		meta_data_hdr->size += meta_item->sz;
		meta_data_hdr->size += METADATA_ITEM_RAW_SZ;
		meta_data_hdr->size += METADATA_UNIT_SEPARATOR_SZ;

		continue;
	}

	/* Finalize MD5 digest and save into METADATA */
	BRB_MD5Final(&md5_digest);
	memcpy(&meta_data_hdr->digest, &md5_digest.digest, sizeof(meta_data_hdr->digest));

	//printf("MetaDataHeaderLoadData - DIGEST [%s] - SIZE [%ld]\n", md5_digest.string, meta_data_hdr->size);

	return 1;
}
/**************************************************************************************************************************/
