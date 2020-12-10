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

//#define windowBits 15
//#define ENABLE_ZLIB_GZIP 32
//#define CHUNK 0x4000

//static int GzipInflateMemBuffer(MemBuffer *in_mb, MemBuffer *out_mb);
//static int GzipDeflateMemBuffer(MemBuffer *in_mb, MemBuffer *out_mb);
static int testGzip(void);

/**************************************************************************************************************************/
static long ConnDBEngineTableFileDeflate(char *tit_ptr, char *data_ptr, unsigned long data_sz)
{
	/* WARNING - FUNCTION IS RUNNING INSIDE ANOTHER THREAD CONTEXT */
	z_stream strm;
	unsigned char out_buf_data[0x4000];
	unsigned char *out_buf_ptr;
	unsigned long out_buf_space;
	unsigned long out_buf_sz;
	int op_status;

	MemBuffer *gz_mb	= NULL;
	long wrote_total	= 0;

	/* Clean up stack */
	memset(&strm, 0, sizeof(z_stream));

	/* Point to buffer in stack area */
	out_buf_ptr		= (unsigned char *)&out_buf_data;
	out_buf_sz		= (sizeof(out_buf_data));

	strm.next_in 	= (unsigned char *)data_ptr;
	strm.avail_in 	= data_sz;
	strm.total_in  	= strm.avail_in;
	strm.next_out  	= out_buf_ptr;
	strm.avail_out 	= out_buf_sz;
	strm.total_out 	= strm.avail_out;

	/* Initialize ZLIB context */
	op_status 		= deflateInit(&strm, Z_BEST_COMPRESSION);	//Z_BEST_SPEED 	Z_BEST_COMPRESSION 	Z_DEFAULT_COMPRESSION

	/* Failed initializing ZLIP context */
	if (Z_OK != op_status)
		return -1;

	/* Initialize MB if we are working on memory */
	gz_mb = MemBufferNew(BRBDATA_THREAD_UNSAFE, 65535);

	/* Begin DEFLATING data */
	do
	{
		strm.avail_out	= out_buf_sz;
		strm.next_out 	= out_buf_ptr;

		op_status 		= deflate(&strm, Z_FINISH);

		if (op_status < 0)
		{
			break;
		}

		/* Calculate size and write */
		out_buf_space 	= (out_buf_sz - strm.avail_out);

		/* We are not compressing on memory, so keep writing to disk */
		MemBufferAdd(gz_mb, out_buf_ptr, out_buf_space);

		continue;
	}
	while (strm.avail_out == 0);


	printf("%s: data_sz %lu - out %lu\n", tit_ptr, data_sz, MemBufferGetSize(gz_mb));

	/* Destroy MB if we are working on memory */
	MemBufferDestroy(gz_mb);

	/* Finish DEFLATE context */
	op_status = deflateEnd(&strm);

	/* Failed finishing context */
	if (Z_OK != op_status)
		return -1;

	return wrote_total;
}
///**************************************************************************************************************************/
//static int GzipInflateMemBuffer(MemBuffer *in_mb, MemBuffer *out_mb)
//{
//	unsigned char out_buf_data[CHUNK];
//	unsigned char *out_buf_ptr;
//	long out_buf_sz;
//	z_stream strm;
//	unsigned have;
//
//	int op_status;
//
//	memset(&strm, 0, sizeof(z_stream));
//
//	strm.zalloc 	= 0;
//	strm.zfree 		= 0;
//	strm.opaque 	= 0;
//
//	strm.next_in 	= (unsigned char *)MemBufferDeref(in_mb);
//	strm.avail_in 	= MemBufferGetSize(in_mb);
//	strm.total_in  	= strm.avail_in;
//
//	out_buf_ptr		= (unsigned char *)&out_buf_data;
//	out_buf_sz		= CHUNK;
//
//	strm.next_out  	= out_buf_ptr;
//	strm.avail_out 	= out_buf_sz;
//	strm.total_out 	= strm.avail_out;
//
//	op_status 		= inflateInit2(&strm, windowBits + ENABLE_ZLIB_GZIP);
//
//	if (Z_OK != op_status)
//	{
//		printf("OP_STATUS [%d] - MSG [%s]\n", op_status, strm.msg);
//
//		return 0;
//	}
//
//	/* Open the file. */
//	do {
//
//		strm.avail_out	= out_buf_sz;
//		strm.next_out 	= out_buf_ptr;
//
//		op_status 		= inflate(&strm, Z_NO_FLUSH);
//
//		if (op_status < 0)
//		{
//			printf("OP_STATUS [%d] - MSG [%s]\n", op_status, strm.msg);
//
//			return 0;
//		}
//
//		printf("OP_STATUS [%d] - OUT [%u] - MSG [%s]\n", op_status, strm.avail_out, strm.msg);
//
//		have 			= out_buf_sz - strm.avail_out;
//
//		MemBufferAdd(out_mb, out_buf_ptr, (sizeof (unsigned char) * have));
//	}
//	while (strm.avail_out == 0);
//
//	op_status 			= inflateEnd(&strm);
//
//	if (Z_OK != op_status)
//	{
//		printf("OP_STATUS [%d]\n", op_status);
//
//		return 0;
//	}
//
//	printf("SIZE [%lu] - DEC [%lu]\n", MemBufferGetSize(in_mb), MemBufferGetSize(out_mb));
//
//	return 1;
//}
///**************************************************************************************************************************/
//static int GzipDeflateMemBuffer(MemBuffer *in_mb, MemBuffer *out_mb)
//{
//	unsigned char out_buf_data[CHUNK];
//	unsigned char *out_buf_ptr;
//	long out_buf_sz;
//	z_stream strm;
//	unsigned have;
//
//	int op_status;
//
//	memset(&strm, 0, sizeof(z_stream));
//
//	strm.zalloc 	= Z_NULL;
//	strm.zfree 		= Z_NULL;
//	strm.opaque 	= Z_NULL;
//
//	strm.next_in 	= (unsigned char *)MemBufferDeref(in_mb);
//	strm.avail_in 	= MemBufferGetSize(in_mb);
//	strm.total_in  	= strm.avail_in;
//
//	out_buf_ptr		= (unsigned char *)&out_buf_data;
//	out_buf_sz		= CHUNK;
//
//	strm.next_out  	= out_buf_ptr;
//	strm.avail_out 	= out_buf_sz;
//	strm.total_out 	= strm.avail_out;
//
//	op_status 		= deflateInit(&strm, Z_DEFAULT_COMPRESSION);
////	op_status 		= deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits + ENABLE_ZLIB_GZIP, 8, Z_DEFAULT_STRATEGY);
//
//	if (Z_OK != op_status)
//	{
//		printf("OP_STATUS [%d] - MSG [%s]\n", op_status, strm.msg);
//
//		return 0;
//	}
//
//	/* Open the file. */
//	do {
//
//		strm.avail_out	= out_buf_sz;
//		strm.next_out 	= out_buf_ptr;
//
//		op_status 		= deflate(&strm, Z_FINISH);
//
//		if (op_status < 0)
//		{
//			printf("OP_STATUS [%d] - MSG [%s]\n", op_status, strm.msg);
//
//			return 0;
//		}
//
//		printf("OP_STATUS [%d] - BUF [%ld] - OUT [%u] - MSG [%s]\n", op_status, out_buf_sz, strm.avail_out, strm.msg);
//
//		have 			= out_buf_sz - strm.avail_out;
//
//		MemBufferAdd(out_mb, out_buf_ptr, (sizeof (unsigned char) * have));
//	}
//	while (strm.avail_out == 0);
//
//	op_status 			= deflateEnd(&strm);
//
//	if (Z_OK != op_status)
//	{
//		printf("OP_STATUS [%d]\n", op_status);
//
//		return 0;
//	}
//
//	printf("SIZE [%lu] - DEC [%lu]\n", MemBufferGetSize(in_mb), MemBufferGetSize(out_mb));
//
//	return 1;
//}
/************************************************************************************************************************/
static int testGzip(void)
{
	MemBuffer *in_mb;
	MemBuffer *deflate_mb;
	MemBuffer *inflate_mb;
	int op_status;
	int i;

	in_mb 			= MemBufferNew(BRBDATA_THREAD_UNSAFE, 2048);
	deflate_mb 		= MemBufferNew(BRBDATA_THREAD_UNSAFE, 2048);
	inflate_mb 		= MemBufferNew(BRBDATA_THREAD_UNSAFE, 2048);

	/* Create IN buffer */
	for (i = 0; i < 32; i++)
	{
		MemBufferPrintf(in_mb, "%s", "321-abc-654-abc-987-321-abc-654-abc-987-321-abc-654-abc-987-321-abc-654-abc-987|");
		continue;
	}

	printf("ORIG MB SIZE [%ld] -> [%s]\n", MemBufferGetSize(in_mb), MemBufferDeref(in_mb));

	/* Begin DEFLATE */
	op_status = MemBufferGzipDeflate(in_mb, 0, deflate_mb);
	printf("DEFLATE to [%ld] bytes with status [%d]\n", MemBufferGetSize(deflate_mb), op_status);

	printf("--------------------\n");

	op_status = MemBufferGzipInflate(deflate_mb, 0, inflate_mb);
	printf("INFLATE to [%ld] bytes with status [%d]\n", MemBufferGetSize(inflate_mb), op_status);
	printf("--------------------\n");
	printf("INFLATE CONTENT [%s]\n", MemBufferDeref(inflate_mb));

	if (!strcmp(MemBufferDeref(in_mb), MemBufferDeref(inflate_mb)))
		printf("CONTENT MATCH\n");
	else
		printf("CONTENT MISMATCH\n");

	return 1;
}
/************************************************************************************************************************/
typedef struct _ConnDBRecordSimple
{
	uint8_t prot;		/* 1 */
	uint8_t start_d:4;
	uint8_t stop_d:4;
	uint8_t start_h;
	uint8_t start_i;
	uint8_t start_s;
	uint8_t stop_h;
	uint8_t stop_i;
	uint8_t stop_s;

	struct in_addr  sa;	/* 4 */
	struct in_addr  da;	/* 4 */
	uint16_t sp;		/* 2 */
	uint16_t dp;		/* 2 */

} ConnDBRecordSimple;
/************************************************************************************************************************/
typedef struct _ConnDBNAT44RecordNAT44
{
	ConnDBRecordSimple simple;

	struct {
		struct in_addr  sa;
		struct in_addr  da;
		uint16_t sp;
		uint16_t dp;
	} nat;

} ConnDBRecordNAT44;
/************************************************************************************************************************/
int main(int argc, char **argv)
{
	MemBuffer *live_rec_ptr;
	MemBuffer *live_nat_ptr;

	MemBuffer *packed_mb_ptr;
	MemBuffer *rebuilt_mb_ptr;
	int i;
	int items = 1000000;

//	testGzip();
//	return 0;

	live_rec_ptr 	= MemBufferNew(BRBDATA_THREAD_UNSAFE, 2048);
	live_nat_ptr 	= MemBufferNew(BRBDATA_THREAD_UNSAFE, 2048);

	ConnDBRecordSimple data_rec 	= {0};
	ConnDBRecordNAT44 data_nat 		= {0};

	for (int i = 0; i < items; i++)
	{
		data_rec.sa.s_addr 		= rand() % 0xFFFFFFFF;
		data_rec.sp 			= rand() % 0xFFFF;
		data_rec.da.s_addr 		= rand() % 0xFFFFFFFF;
		data_rec.dp 			= rand() % 0xFFFF;

		data_nat.nat.sa.s_addr 	= rand() % 0xFFFFFFFF;
		data_nat.nat.sp 		= rand() % 0xFFFF;
		data_nat.nat.da.s_addr 	= rand() % 0xFFFFFFFF;
		data_nat.nat.dp 		= rand() % 0xFFFF;

//		printf("IP %s\n", inet_ntoa(data_rec.nat.sa));

		memcpy(&data_nat.simple, &data_rec, sizeof(ConnDBRecordSimple));
		MemBufferAdd(live_rec_ptr, &data_rec, sizeof(ConnDBRecordSimple));
		MemBufferAdd(live_nat_ptr, &data_nat, sizeof(ConnDBRecordNAT44));
	}

	ConnDBEngineTableFileDeflate("REC", MemBufferDeref(live_rec_ptr), MemBufferGetSize(live_rec_ptr));
	ConnDBEngineTableFileDeflate("NAT", MemBufferDeref(live_nat_ptr), MemBufferGetSize(live_nat_ptr));

	MemBufferClean(live_rec_ptr);
	MemBufferClean(live_nat_ptr);

	printf("\n");
	printf("ZEROED\n");

	for (int i = 0; i < items; i++)
	{
		data_rec.sa.s_addr 		= rand() % 0xFFFFFFFF;
		data_rec.sp 			= rand() % 0xFFFF;
		data_rec.da.s_addr 		= rand() % 0xFFFFFFFF;
		data_rec.dp 			= rand() % 0xFFFF;

		data_nat.nat.sa.s_addr 	= 0;
		data_nat.nat.sp 		= 0;
		data_nat.nat.da.s_addr 	= 0;
		data_nat.nat.dp 		= 0;

//		printf("IP %s\n", inet_ntoa(data_rec.nat.sa));

		memcpy(&data_nat.simple, &data_rec, sizeof(ConnDBRecordSimple));
		MemBufferAdd(live_rec_ptr, &data_rec, sizeof(ConnDBRecordSimple));
		MemBufferAdd(live_nat_ptr, &data_nat, sizeof(ConnDBRecordNAT44));
	}

	ConnDBEngineTableFileDeflate("REC", MemBufferDeref(live_rec_ptr), MemBufferGetSize(live_rec_ptr));
	ConnDBEngineTableFileDeflate("NAT", MemBufferDeref(live_nat_ptr), MemBufferGetSize(live_nat_ptr));

//	MemBufferPrintf(live_mb_ptr, "%s", "abcdef");
//	printf("MB [%s]\n", MemBufferDeref(live_mb_ptr));

//	MemBufferRemoveLastChar(live_mb_ptr);
//	printf("MB [%s]\n", MemBufferDeref(live_mb_ptr));
//
//	MemBufferPrintf(live_mb_ptr, "%s", "abcdef");
//	printf("MB [%s]\n", MemBufferDeref(live_mb_ptr));
//
//	return 1;
//
//	/* Create the buffer */
//	for (i = 0; i < 128; i++)
//	{
//		MemBufferPrintf(live_mb_ptr, "this is the line %d with random seed [%02X]\n", i, arc4random());
//		continue;
//	}
//
//	/* Pack MB from live MB */
//	packed_mb_ptr = MemBufferMetaDataPackToMB(live_mb_ptr, 0);
//	rebuilt_mb_ptr = MemBufferMetaDataUnPackFromMB(packed_mb_ptr);
//
//
//	MemBufferWriteToFile(packed_mb_ptr, "./mem_buf.dump");
//
//	MemBufferShow(live_mb_ptr, stdout);
//	MemBufferShow(rebuilt_mb_ptr, stdout);

	return 0;
}
/************************************************************************************************************************/
