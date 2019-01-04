/*
 * test_mem_buf.c
 *
 *  Created on: 2012-01-12
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2012 BrByte Software (Oliveira Alves & Amorim LTDA)
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

#include "zlib.h"

#define windowBits 15
#define ENABLE_ZLIB_GZIP 32
#define CHUNK 0x4000

static int GzipInflateMemBuffer(MemBuffer *in_mb, MemBuffer *out_mb);
static int GzipDeflateMemBuffer(MemBuffer *in_mb, MemBuffer *out_mb);
static int testGzip(void);

/**************************************************************************************************************************/
static int GzipInflateMemBuffer(MemBuffer *in_mb, MemBuffer *out_mb)
{
	unsigned char out_buf_data[CHUNK];
	unsigned char *out_buf_ptr;
	long out_buf_sz;
	z_stream strm;
	unsigned have;

	int op_status;

	memset(&strm, 0, sizeof(z_stream));

	strm.zalloc 	= 0;
	strm.zfree 		= 0;
	strm.opaque 	= 0;

	strm.next_in 	= (unsigned char *)MemBufferDeref(in_mb);
	strm.avail_in 	= MemBufferGetSize(in_mb);
	strm.total_in  	= strm.avail_in;

	out_buf_ptr		= (unsigned char *)&out_buf_data;
	out_buf_sz		= CHUNK;

	strm.next_out  	= out_buf_ptr;
	strm.avail_out 	= out_buf_sz;
	strm.total_out 	= strm.avail_out;

	op_status 		= inflateInit2(&strm, windowBits + ENABLE_ZLIB_GZIP);

	if (Z_OK != op_status)
	{
		printf("OP_STATUS [%d] - MSG [%s]\n", op_status, strm.msg);

		return 0;
	}

	/* Open the file. */
	do {

		strm.avail_out	= out_buf_sz;
		strm.next_out 	= out_buf_ptr;

		op_status 		= inflate(&strm, Z_NO_FLUSH);

		if (op_status < 0)
		{
			printf("OP_STATUS [%d] - MSG [%s]\n", op_status, strm.msg);

			return 0;
		}

		printf("OP_STATUS [%d] - OUT [%u] - MSG [%s]\n", op_status, strm.avail_out, strm.msg);

		have 			= out_buf_sz - strm.avail_out;

		MemBufferAdd(out_mb, out_buf_ptr, (sizeof (unsigned char) * have));
	}
	while (strm.avail_out == 0);

	op_status 			= inflateEnd(&strm);

	if (Z_OK != op_status)
	{
		printf("OP_STATUS [%d]\n", op_status);

		return 0;
	}

	printf("SIZE [%lu] - DEC [%lu]\n", MemBufferGetSize(in_mb), MemBufferGetSize(out_mb));

	return 1;
}
/**************************************************************************************************************************/
static int GzipDeflateMemBuffer(MemBuffer *in_mb, MemBuffer *out_mb)
{
	unsigned char out_buf_data[CHUNK];
	unsigned char *out_buf_ptr;
	long out_buf_sz;
	z_stream strm;
	unsigned have;

	int op_status;

	memset(&strm, 0, sizeof(z_stream));

	strm.zalloc 	= Z_NULL;
	strm.zfree 		= Z_NULL;
	strm.opaque 	= Z_NULL;

	strm.next_in 	= (unsigned char *)MemBufferDeref(in_mb);
	strm.avail_in 	= MemBufferGetSize(in_mb);
	strm.total_in  	= strm.avail_in;

	out_buf_ptr		= (unsigned char *)&out_buf_data;
	out_buf_sz		= CHUNK;

	strm.next_out  	= out_buf_ptr;
	strm.avail_out 	= out_buf_sz;
	strm.total_out 	= strm.avail_out;

	op_status 		= deflateInit(&strm, Z_DEFAULT_COMPRESSION);
//	op_status 		= deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits + ENABLE_ZLIB_GZIP, 8, Z_DEFAULT_STRATEGY);

	if (Z_OK != op_status)
	{
		printf("OP_STATUS [%d] - MSG [%s]\n", op_status, strm.msg);

		return 0;
	}

	/* Open the file. */
	do {

		strm.avail_out	= out_buf_sz;
		strm.next_out 	= out_buf_ptr;

		op_status 		= deflate(&strm, Z_FINISH);

		if (op_status < 0)
		{
			printf("OP_STATUS [%d] - MSG [%s]\n", op_status, strm.msg);

			return 0;
		}

		printf("OP_STATUS [%d] - BUF [%ld] - OUT [%u] - MSG [%s]\n", op_status, out_buf_sz, strm.avail_out, strm.msg);

		have 			= out_buf_sz - strm.avail_out;

		MemBufferAdd(out_mb, out_buf_ptr, (sizeof (unsigned char) * have));
	}
	while (strm.avail_out == 0);

	op_status 			= deflateEnd(&strm);

	if (Z_OK != op_status)
	{
		printf("OP_STATUS [%d]\n", op_status);

		return 0;
	}

	printf("SIZE [%lu] - DEC [%lu]\n", MemBufferGetSize(in_mb), MemBufferGetSize(out_mb));

	return 1;
}
/************************************************************************************************************************/
static int testGzip(void)
{
	MemBuffer *in_mb;
	MemBuffer *deflate_mb;
	MemBuffer *inflate_mb;

	in_mb 			= MemBufferNew(BRBDATA_THREAD_UNSAFE, 2048);
	deflate_mb 		= MemBufferNew(BRBDATA_THREAD_UNSAFE, 2048);
	inflate_mb 		= MemBufferNew(BRBDATA_THREAD_UNSAFE, 2048);

	MemBufferPrintf(in_mb, "%s", "321-abc-654-abc-987-");
	MemBufferPrintf(in_mb, "%s", "321-abc-654-abc-987-");
	MemBufferPrintf(in_mb, "%s", "321-abc-654-abc-987-");

	printf("MB [%s]\n", MemBufferDeref(in_mb));
	printf(" DEFLATE -----------\n");
	GzipDeflateMemBuffer(in_mb, deflate_mb);
	printf("--------------------\n");
	printf(" INFLATE -----------\n");
	GzipInflateMemBuffer(deflate_mb, inflate_mb);
	printf("MB [%s]\n", MemBufferDeref(inflate_mb));
	printf("--------------------\n");

	return 1;
}
/************************************************************************************************************************/
int main(int argc, char **argv)
{
	MemBuffer *live_mb_ptr;
	MemBuffer *packed_mb_ptr;
	MemBuffer *rebuilt_mb_ptr;
	int i;

	testGzip();

	live_mb_ptr = MemBufferNew(BRBDATA_THREAD_UNSAFE, 2048);

	MemBufferPrintf(live_mb_ptr, "%s", "abcdef");
	printf("MB [%s]\n", MemBufferDeref(live_mb_ptr));

	MemBufferRemoveLastChar(live_mb_ptr);
	printf("MB [%s]\n", MemBufferDeref(live_mb_ptr));

	MemBufferPrintf(live_mb_ptr, "%s", "abcdef");
	printf("MB [%s]\n", MemBufferDeref(live_mb_ptr));

	return 1;

	/* Create the buffer */
	for (i = 0; i < 128; i++)
	{
		MemBufferPrintf(live_mb_ptr, "this is the line %d with random seed [%02X]\n", i, arc4random());
		continue;
	}


	/* Pack MB from live MB */
	packed_mb_ptr = MemBufferMetaDataPackToMB(live_mb_ptr, 0);
	rebuilt_mb_ptr = MemBufferMetaDataUnPackFromMB(packed_mb_ptr);


	MemBufferWriteToFile(packed_mb_ptr, "./mem_buf.dump");


	MemBufferShow(live_mb_ptr, stdout);
	MemBufferShow(rebuilt_mb_ptr, stdout);


	return 0;
}
/************************************************************************************************************************/
