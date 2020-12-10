/*
 * ev_kq_aio_transform.c
 *
 *  Created on: 2014-11-03
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

static MemBuffer *EvAIOReqTransform_CryptoRaw(CommEvContentTransformerInfo *transform_info, char *in_data_ptr, long data_sz, int crypto_operation);
static MemBuffer *EvAIOReqTransform_CryptoAIOReq(CommEvContentTransformerInfo *transform_info, EvAIOReq *aio_req);

/**************************************************************************************************************************/
int EvAIOReqTransform_WriteData(void *transform_info_ptr, EvAIOReqQueue *aio_req_queue, EvAIOReq *aio_req)
{
	CommEvContentTransformerInfo *transform_info	= transform_info_ptr;
	CommEvCryptoInfo *crypto						= &transform_info->crypto;

	/* Perform data TRANSFORMATIONS */
	aio_req->transformed_mb = EvAIOReqTransform_CryptoAIOReq(transform_info, aio_req);

	/* Request has been transformed, set flags */
	if (aio_req->transformed_mb)
	{
		aio_req->flags.transformed = 1;
		return 1;
	}
	else
	{
		return 0;
	}

	return 0;
}
/**************************************************************************************************************************/
MemBuffer *EvAIOReqTransform_ReadData(void *transform_info_ptr, char *in_data_ptr, long data_sz)
{
	CommEvContentTransformerInfo *transform_info	= transform_info_ptr;
	CommEvCryptoInfo *crypto						= &transform_info->crypto;
	MemBuffer *transformed_mb						= NULL;

	/* Perform data TRANSFORMATIONS */
	transformed_mb = EvAIOReqTransform_CryptoRaw(transform_info, in_data_ptr, data_sz, CRYPTO_OPERATION_READ);

	return transformed_mb;
}
/**************************************************************************************************************************/
int EvAIOReqTransform_CryptoEnable(void *transform_info_ptr, int algo_code, char *key_ptr, int key_sz)
{
	CommEvContentTransformerInfo *transform_info	= transform_info_ptr;
	CommEvCryptoInfo *crypto						= &transform_info->crypto;

	/* Clean up internal states */
	memset(&crypto->state, 0, sizeof(crypto->state));

	/* Initialize based on ALGO_CODE */
	switch(algo_code)
	{
	case COMM_CRYPTO_FUNC_RC4:
	case COMM_CRYPTO_FUNC_RC4_MD5:
	{
		BRB_RC4_Init(&crypto->state.read.rc4, (unsigned char*)key_ptr, key_sz);
		BRB_RC4_Init(&crypto->state.write.rc4, (unsigned char*)key_ptr, key_sz);
		break;
	}
	case COMM_CRYPTO_FUNC_BLOWFISH:
	{
		BRB_Blowfish_Init(&crypto->state.read.blowfish, (unsigned char*)key_ptr, key_sz);
		BRB_Blowfish_Init(&crypto->state.write.blowfish, (unsigned char*)key_ptr, key_sz);
		break;
	}

	default:						return 0;
	}

	/* Save ALGORITHM and enable CRYPTO */
	crypto->algo_code		= algo_code;
	crypto->flags.enabled	= 1;

	return 1;
}
/**************************************************************************************************************************/
int EvAIOReqTransform_CryptoDisable(void *transform_info_ptr)
{
	CommEvContentTransformerInfo *transform_info	= transform_info_ptr;
	CommEvCryptoInfo *crypto						= &transform_info->crypto;

	/* DISABLE CRYPTO */
	crypto->algo_code		= COMM_CRYPTO_FUNC_NONE;
	crypto->flags.enabled	= 0;

	/* Clean up internal states */
	memset(&crypto->state, 0, sizeof(crypto->state));

	return 1;
}
/**************************************************************************************************************************/
char *EvAIOReqTransform_RC4_MD5_DataHashString(MemBuffer *transformed_mb)
{
//	BRB_MD5_CTX md5_ctx;
//	char *ret_ptr 		= (char*)&md5_ctx.string;
//
//	char *known_string_ptr		= NULL;
//	char *digest_ptr			= NULL;
//
//	known_string_ptr	= ((char*)transformed_mb->data + sizeof(unsigned long));
//	digest_ptr			= ((char*)transformed_mb->data + sizeof(unsigned long) + 5);
//
//	/* Digest DATA */
//	memset(&md5_ctx, 0, sizeof(md5_ctx));
//	BRB_MD5Init(&md5_ctx);
//	BRB_MD5Update(&md5_ctx, MemBufferDeref(transformed_mb), MemBufferGetSize(transformed_mb));
//	BRB_MD5Final(&md5_ctx);
//
//	return ret_ptr;

	/*
	 * event/aio/ev_kq_aio_transform.c:142:9: warning: function returns address of local variable [-Wreturn-local-addr]
  return ret_ptr;
         ^~~~~~~
event/aio/ev_kq_aio_transform.c:127:14: note: declared here
  BRB_MD5_CTX md5_ctx;
              ^~~~~~~
	 *
	 */

	return NULL;

}
/**************************************************************************************************************************/
int EvAIOReqTransform_RC4_MD5_DataValidate(MemBuffer *transformed_mb)
{
	BRB_MD5_CTX md5_ctx;

	char *known_string_ptr		= NULL;
	char *digest_ptr			= NULL;

	/* Do not USE MEMBUFFER_DEREF because we want data behind current OFFSET */
	known_string_ptr	= ((char*)transformed_mb->data + sizeof(unsigned long));
	digest_ptr			= ((char*)transformed_mb->data + sizeof(unsigned long) + 5);

	/* Check KNWON string */
	if (memcmp(known_string_ptr, "HASH:", 5))
		return 0;

	/* Digest DATA */
	memset(&md5_ctx, 0, sizeof(md5_ctx));
	BRB_MD5Init(&md5_ctx);
	BRB_MD5Update(&md5_ctx, MemBufferDeref(transformed_mb), MemBufferGetSize(transformed_mb));
	BRB_MD5Final(&md5_ctx);

	/* Check DATA HASH */
	if (memcmp(&md5_ctx.digest, digest_ptr, sizeof(md5_ctx.digest)))
		return 0;

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static MemBuffer *EvAIOReqTransform_CryptoRaw(CommEvContentTransformerInfo *transform_info, char *in_data_ptr, long data_sz, int crypto_operation)
{
	BRB_MD5_CTX md5_ctx;
	unsigned char *transformed_uptr;
	long transformed_mb_sz;

	MemBuffer *transformed_mb	= NULL;
	CommEvCryptoInfo *crypto	= &transform_info->crypto;
	unsigned long random_salt	= arc4random();

	/* CRYPTO is enabled, process it */
	if (crypto->flags.enabled)
	{
		switch(crypto->algo_code)
		{
		case COMM_CRYPTO_FUNC_BLOWFISH:
		{
			printf("COMM_CRYPTO_FUNC_BLOWFISH - Not implemented\n");
			/* NOT IMPLEMENTED */
			break;
		}

		case COMM_CRYPTO_FUNC_RC4_MD5:
		{
			/* Create a new buffer to hold transformed DATA */
			transformed_mb	= MemBufferNew(BRBDATA_THREAD_UNSAFE, (data_sz + 64));

			/* We are writing, append digested DATA */
			if (CRYPTO_OPERATION_WRITE == crypto_operation)
			{
				/* Digest DATA */
				memset(&md5_ctx, 0, sizeof(md5_ctx));
				BRB_MD5Init(&md5_ctx);
				BRB_MD5Update(&md5_ctx, (const void*)in_data_ptr, data_sz);
				BRB_MD5Final(&md5_ctx);

				/* Append RANDOM SALT to change external CRYPTO data for observer and append MD5 of data */
				MemBufferAdd(transformed_mb, (char*)&random_salt, sizeof(unsigned long));
				MemBufferAdd(transformed_mb, "HASH:", 5);
				MemBufferAdd(transformed_mb, (char*)&md5_ctx.digest, sizeof(md5_ctx.digest));
				MemBufferAdd(transformed_mb, "\0", 1);

				/* Append DATA */
				MemBufferAdd(transformed_mb, (unsigned char*)in_data_ptr, data_sz);
			}
			/* We are reading, bypass DIGEST and HEADER DATA */
			else if (CRYPTO_OPERATION_READ == crypto_operation)
			{
				/* Append DATA */
				MemBufferAdd(transformed_mb, (unsigned char*)in_data_ptr, data_sz);
			}

			goto rc4_transform;

			break;
		}
		case COMM_CRYPTO_FUNC_RC4:
		{
			/* Create a new buffer to hold transformed DATA */
			transformed_mb	= MemBufferNew(BRBDATA_THREAD_UNSAFE, (data_sz + 64));
			MemBufferAdd(transformed_mb, (unsigned char*)in_data_ptr, data_sz);
			goto rc4_transform;

			break;
		}
		default:
			break;
		}
	}


	return transformed_mb;

	/* TAG for RC4 encryption */
	rc4_transform:

	transformed_uptr	= (unsigned char*)MemBufferDeref(transformed_mb);
	transformed_mb_sz	= MemBufferGetSize(transformed_mb);

	/* Perform actual encryption */
	switch (crypto_operation)
	{
	case CRYPTO_OPERATION_READ:
	{
		BRB_RC4_Crypt(&crypto->state.read.rc4, transformed_uptr, transformed_uptr, transformed_mb_sz);

		/* Bypass SALT, KNOWN STRING, HASH and NULL separator */
		if (crypto->algo_code == COMM_CRYPTO_FUNC_RC4_MD5)
			MemBufferOffsetSet(transformed_mb, (sizeof(unsigned long) + 5 + 16 + 1));

		break;
	}
	case CRYPTO_OPERATION_WRITE:
		BRB_RC4_Crypt(&crypto->state.write.rc4, transformed_uptr, transformed_uptr, transformed_mb_sz);
		break;
	}

	return transformed_mb;

}
/**************************************************************************************************************************/
static MemBuffer *EvAIOReqTransform_CryptoAIOReq(CommEvContentTransformerInfo *transform_info, EvAIOReq *aio_req)
{
	CommEvCryptoInfo *crypto	= &transform_info->crypto;
	char *in_data_ptr			= aio_req->data.ptr;
	long data_sz				= aio_req->data.size;
	MemBuffer *transformed_mb	= NULL;

	transformed_mb = EvAIOReqTransform_CryptoRaw(transform_info, in_data_ptr, data_sz, CRYPTO_OPERATION_WRITE);

	return transformed_mb;
}
/**************************************************************************************************************************/
