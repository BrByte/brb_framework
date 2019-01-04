/*
 * test_meta_data.c
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

#include <libbrb_core.h>

typedef struct _GenericStruct
{
	int a;
	long b;
	float c;
	char buf[32];
} GenericStruct;

typedef enum
{
	ITEM_GENERIC_STRUCT = 51,
	ITEM_STRING,
	ITEM_LASTITEM
} ItemCodes;

/************************************************************************************************************************/
int main(int argc, char **argv)
{
	MetaDataItem *meta_item;
	MetaData meta_data_pack;
	MetaData meta_data_unpack;
	GenericStruct generic_struct;
	GenericStruct *generic_struct_ptr;
	MemBuffer *packed_mb;
	int i;

	/* Create buffer to hold packed data and PACK */
	packed_mb = MemBufferNew(BRBDATA_THREAD_UNSAFE, 512);

	/* Clean up stack */
	memset(&meta_data_pack, 0, sizeof(MetaData));
	memset(&meta_data_unpack, 0, sizeof(MetaData));
	memset(&generic_struct, 0, sizeof(GenericStruct));

	/* Populate generic structure */
	generic_struct.a = 500;
	generic_struct.b = 99999;
	generic_struct.c = 45.66;
	strlcpy(generic_struct.buf, "aaaaaaaaa", sizeof(generic_struct.buf));

	/* Add item into META */
	MetaDataItemAdd(&meta_data_pack, ITEM_GENERIC_STRUCT, 0, &generic_struct, sizeof(GenericStruct));
	MetaDataItemAdd(&meta_data_pack, ITEM_STRING, 0, "este eh um item do metadado", (strlen("este eh um item do metadado") + 1));
	MetaDataItemAdd(&meta_data_pack, ITEM_STRING, 0, "oiiiiiiiie", (strlen("oiiiiiiiie") + 1));
	MetaDataItemAdd(&meta_data_pack, ITEM_GENERIC_STRUCT, 0, &generic_struct, sizeof(GenericStruct));
	MetaDataItemAdd(&meta_data_pack, ITEM_STRING, 0, "este eh um item do metadado", (strlen("este eh um item do metadado") + 1));
	MetaDataItemAdd(&meta_data_pack, ITEM_GENERIC_STRUCT, 0, &generic_struct, sizeof(GenericStruct));
	MetaDataItemAdd(&meta_data_pack, ITEM_GENERIC_STRUCT, 0, &generic_struct, sizeof(GenericStruct));


	MetaDataPack(&meta_data_pack, packed_mb);

	/* Now unpack METADATA */
	MetaDataUnpack(&meta_data_unpack, packed_mb, NULL);

	MemBufferWriteToFile(packed_mb, "./metadata.dump");

	for (i = 0; i < meta_data_unpack.items.count; i++)
	{
		meta_item = MemArenaGrabByID(meta_data_unpack.items.arena, i);

		if (ITEM_STRING == meta_item->item_id)
		{
			printf("INDEX [%d] - STRING_ITEM - [%lu]-[%s]\n", i, meta_item->sz, meta_item->ptr);
		}
		else if (ITEM_GENERIC_STRUCT == meta_item->item_id)
		{
			generic_struct_ptr = meta_item->ptr;

			printf("INDEX [%d] - BINARY ITEM ID [%lu] - A [%d] - B [%ld] - C [%f] - BUF [%s]\n",
					i, meta_item->item_id, generic_struct_ptr->a, generic_struct_ptr->b, generic_struct_ptr->c, generic_struct_ptr->buf);
		}
		else
		{
			printf("INDEX [%d] - BINARY ITEM ID [%lu]\n", i, meta_item->item_id);
		}



		continue;
	}

	return 1;
}
/************************************************************************************************************************/
