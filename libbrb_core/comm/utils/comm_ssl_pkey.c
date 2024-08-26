/*
 * comm_ssl_pkey.c
 *
 *  Created on: 2014-04-30
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

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/ecdsa.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>

/**************************************************************************************************************************/
void CommEvSSLUtils_X509PrivateKeyRefCountInc(EVP_PKEY *key, int thread_safe)
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L
	if (thread_safe)
		CRYPTO_add(&key->references, 1, CRYPTO_LOCK_EVP_PKEY);
	else
		key->references++;
#else
	EVP_PKEY_up_ref(key);
#endif

	return;
}
/************************************************************************************************************************/
int CommEvSSLUtils_X509PrivateKeyCheck(X509 *cert_x509, EVP_PKEY *cert_key)
{
    int op_status;

    /* Sanitize */
    if (!cert_x509 || !cert_key)
    {
    	errno 		= ENOENT;
        return -2;
    }

	op_status 		= X509_check_private_key(cert_x509, cert_key);

	/* Success */
	if (op_status == 1)
		return 0;

	errno 			= EFAULT;
//	err_code 		= ERR_get_error();
//	err_str 		= ERR_error_string(err_code, NULL);

	return -1;
}
/**************************************************************************************************************************/
EVP_PKEY *CommEvSSLUtils_X509PrivateKeyFromMB(MemBuffer *file_mb)
{
	return CommEvSSLUtils_X509PrivateKeyFromPEM(MemBufferDeref(file_mb), MemBufferGetSize(file_mb));
}
/**************************************************************************************************************************/
EVP_PKEY *CommEvSSLUtils_X509PrivateKeyFromPEM(char *pem_str, int pem_strsz)
{
	BIO *cert_bio;
	EVP_PKEY *priv_key;
	int op_status;

    /* Sanitize */
    if (!pem_str)
        return NULL;

	cert_bio 		= BIO_new(BIO_s_mem());

    if (!cert_bio)
    {
    	errno 		= ENOMEM;
        return NULL;
    }

	/* Load PEM string into BIO */
	op_status 		= BIO_puts(cert_bio, pem_str);

    if (op_status < 0)
    {
    	errno 		= ENOEXEC;
		BIO_free(cert_bio);
        return NULL;
    }

	priv_key 		= PEM_read_bio_PrivateKey(cert_bio, NULL, NULL, NULL);

    if (!priv_key)
    {
    	errno 		= EIO;
    	BIO_free(cert_bio);
        return NULL;
    }

	BIO_free(cert_bio);

	return priv_key;
}
/**************************************************************************************************************************/
void CommEvSSLUtils_X509PrivateKeyToPEM(EVP_PKEY *key, char *ret_buf, int ret_buf_maxsz)
{
	BIO *bio;
	char *p, *ret;
	size_t sz;

	/* NULL terminate return buffer */
	memset(ret_buf, 0, ret_buf_maxsz);

	bio 		= BIO_new(BIO_s_mem());

	if (!bio)
		return;

	PEM_write_bio_PrivateKey(bio, key, NULL, NULL, 0, NULL, NULL);
	sz 			= BIO_get_mem_data(bio, &p);

	/* Copy PEM string into destination buffer */
	strlcpy(ret_buf, p, sz);
	ret_buf[sz] = '\0';

	BIO_free(bio);

	return;
}
/**************************************************************************************************************************/
int CommEvSSLUtils_X509PrivateKeyWriteToFile(const char *filename, EVP_PKEY *pkey)
{
	FILE *file_fp;
	int op_status;

	/* Failed to open file for writing, bail out */
	if (!pkey)
		return -1;

	int ktype 	= EVP_PKEY_base_id(pkey);

    // Check the type of the key
	if (ktype != EVP_PKEY_RSA && ktype != EVP_PKEY_EC)
        return -1;

	/* Try to open file for writing */
	file_fp 	= fopen(filename, "w+");

	/* Failed to open file for writing, bail out */
	if (!file_fp)
		return -2;

    // Check the type of the key
    switch (ktype)
    {
        case EVP_PKEY_RSA:
        {
            // For RSA keys
        	op_status 	= PEM_write_PrivateKey(file_fp, pkey, NULL, NULL, 0, NULL, NULL);
            break;
        }
        case EVP_PKEY_EC:
        {
            // For EC keys
#if OPENSSL_VERSION_NUMBER < 0x10100000L
        	op_status 	= PEM_write_ECPrivateKey(file_fp, EVP_PKEY_get1_EC_KEY(pkey), NULL, NULL, 0, NULL, NULL);
#else
        	op_status 	= PEM_write_ECPrivateKey(file_fp, EVP_PKEY_get0_EC_KEY(pkey), NULL, NULL, 0, NULL, NULL);
#endif
            break;
        }
        default:
        {
            // Unsupported key type
            op_status 	= 0;
            break;
        }
    }

	/* Failed writing, bail out */
	if (op_status != 1)
	{
		fclose(file_fp);
		return -3;
	}

	/* Flush and close */
	fflush(file_fp);
	fclose(file_fp);

	return 0;
}
/**************************************************************************************************************************/
EVP_PKEY *CommEvSSLUtils_X509PrivateKeyReadFromFile(const char *filename)
{
	EVP_PKEY *pkey;
	FILE *file_fp;
	int op_status;

	/* Try to open file for writing */
	file_fp 		= fopen(filename, "r");

	/* Failed to open file for writing, bail out */
	if (!file_fp)
		return NULL;

	pkey			= EVP_PKEY_new();
	PEM_read_PrivateKey(file_fp, &pkey, NULL, NULL);

	/* Close */
	fclose(file_fp);

	return pkey;
}
/**************************************************************************************************************************/
EVP_PKEY *CommEvSSLUtils_X509PrivateKeyReadCreate(const char *path_ptr, int ptype)
{
	EVP_PKEY *pkey;
	int ktype;
	int op_status;

	/* Try to open certificate file */
	pkey 				= CommEvSSLUtils_X509PrivateKeyReadFromFile(path_ptr);

	/* Has Key */
	if (pkey)
	{
		ktype 			= EVP_PKEY_base_id(pkey);
		if (ktype == EVP_PKEY_RSA || ktype == EVP_PKEY_EC)
		{
//			printf("FOUND KEY - [%d][%p]\n", ktype, pkey);
			return pkey;
		}

		/* Reset and remove old file */
		EVP_PKEY_free(pkey);
		pkey 			= NULL;
		unlink(path_ptr);
	}

	/* Check for new key */
	pkey 				= CommEvSSLUtils_X509PrivateKeyCreate(ptype);

	/* Failure to create key */
	if (pkey == NULL)
	{
		errno 			= ENOENT;
//		printf("CAN'T CREATE KEY - [%d]\n", ktype);
		return NULL;
	}

	/* Save file */
	op_status 			= CommEvSSLUtils_X509PrivateKeyWriteToFile(path_ptr, pkey);

	/* Can't save file */
	if (op_status != 0)
	{
//		printf("CAN'T SAVE KEY - [%p]\n", pkey);

		/* Reset info */
		EVP_PKEY_free(pkey);
		pkey 			= NULL;
		errno 			= EIO;
	}

	return pkey;
}
/**************************************************************************************************************************/
EVP_PKEY *CommEvSSLUtils_X509PrivateKeyCreate(int type)
{
	EVP_PKEY *pkey 		= NULL;

    // Check the type of the key
    switch (type)
    {
        case EVP_PKEY_EC:
        {
        	pkey = CommEvSSLUtils_X509PrivateKeyCreateEC(NID_secp384r1);
            break;
        }
        case EVP_PKEY_RSA:
        default:
        {
        	pkey = CommEvSSLUtils_X509PrivateKeyCreateRSA(4096);
            break;
        }
    }

	return pkey;
}
/**************************************************************************************************************************/
EVP_PKEY *CommEvSSLUtils_X509PrivateKeyCreateRSA(int kbits)
{
	EVP_PKEY_CTX *ctx 	= NULL;
	EVP_PKEY *pkey 		= NULL;

	/* First, create the context and the key. */
	ctx 		= EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);

	if (ctx == NULL)
	{
//		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "EVP_PKEY_CTX_new_id");
		goto err;
	}

	if (EVP_PKEY_keygen_init(ctx) <= 0)
	{
//		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "EVP_PKEY_keygen_init");
		goto err;
	}

	if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, kbits) <= 0)
	{
//		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "EVP_PKEY_set_rsa_keygen_bits");
		goto err;
	}

	if (EVP_PKEY_keygen(ctx, &pkey) <= 0)
	{
//		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "EVP_PKEY_keygen");
		goto err;
	}

	goto out;

err:
	EVP_PKEY_free(pkey);
	pkey 		= NULL;
out:
	EVP_PKEY_CTX_free(ctx);
	return pkey;
}
/**************************************************************************************************************************/
EVP_PKEY *CommEvSSLUtils_X509PrivateKeyCreateEC(int knid)
{
	EC_KEY		*eckey = NULL;
	EVP_PKEY	*pkey = NULL;

	eckey 		= EC_KEY_new_by_curve_name(knid);
	if (eckey == NULL)
	{
//		printf("EC_KEY_new_by_curve_name");
		goto err;
	}

	if (!EC_KEY_generate_key(eckey))
	{
//		printf("EC_KEY_generate_key");
		goto err;
	}

	/* set OPENSSL_EC_NAMED_CURVE to be able to load the key */

	EC_KEY_set_asn1_flag(eckey, OPENSSL_EC_NAMED_CURVE);

//	/* Serialise the key to the disc in EC format */
//	if (!PEM_write_ECPrivateKey(f, eckey, NULL, NULL, 0, NULL, NULL))
//	{
//		printf("PEM_write_ECPrivateKey");
//		goto err;
//	}

	/* Convert the EC key into a PKEY structure */
	pkey 		= EVP_PKEY_new();
	if (pkey == NULL)
	{
//		printf("EVP_PKEY_new");
		goto err;
	}

	if (!EVP_PKEY_set1_EC_KEY(pkey, eckey))
	{
//		printf("EVP_PKEY_assign_EC_KEY");
		goto err;
	}

	goto out;

err:
	EC_KEY_free(eckey);
	EVP_PKEY_free(pkey);
	pkey = NULL;
out:
	return pkey;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
int CommEvSSLUtils_GenerateRSAToServer(CommEvTCPServer *srv_ptr, const int keysize)
{
	/* Sanity check */
	if (!srv_ptr)
		return 0;

	/* Generate RSA key */
	srv_ptr->ssldata.rsa_key = RSA_generate_key(keysize, 3, NULL, NULL);

	/* Failed generating RSA key */
	if (!srv_ptr->ssldata.rsa_key)
		return 0;

	/* Generate private key */
	srv_ptr->ssldata.main_key =  EVP_PKEY_new();

	/* Assign private key to RSA key - Does not increment REFCOUNT */
	EVP_PKEY_assign_RSA(srv_ptr->ssldata.main_key, srv_ptr->ssldata.rsa_key);

	return 1;
}
/**************************************************************************************************************************/
void CommEvSSLUtils_GenerateRSA(EVP_PKEY **dst_pkey, RSA **dst_rsa, const int keysize)
{
	dst_rsa[0] = RSA_generate_key(keysize, 3, NULL, NULL);

	if (!dst_rsa[0])
		return;

	dst_pkey[0] = EVP_PKEY_new();

	/* Assign private key to RSA key - Does not increment REFCOUNT */
	EVP_PKEY_assign_RSA(dst_pkey[0], dst_rsa[0]);

	return;
}
/**************************************************************************************************************************/
unsigned char *CommEvSSLUtils_RSASHA1Sign(const char *data_ptr, size_t data_len, EVP_PKEY *private_key, unsigned int *sign_len)
{
	RSA *cert_rsa;
	unsigned char *sign_ptr;
	int op_status;

	/* Sanitize */
	if (!private_key)
		return NULL;

    // Step 2: Calculate the SHA-1 digest of the canonicalized data
    unsigned char digest[SHA_DIGEST_LENGTH];

    BrbSha1_Do(data_ptr, data_len, digest);

//	cert_evp_key 	= BrbXML_LoadKey("xxxx");
	cert_rsa 		= EVP_PKEY_get1_RSA(private_key);

	if (!cert_rsa)
		return NULL;

	/* Allocate new sign */
	sign_ptr		= (unsigned char*)malloc(RSA_size(cert_rsa));

	if (!sign_ptr)
		return NULL;

	*sign_len 		= 0;

	op_status 		= RSA_sign(NID_sha1, digest, SHA_DIGEST_LENGTH, sign_ptr, sign_len, cert_rsa);

	if (op_status != 1)
	{
		free(sign_ptr);
		return NULL;
	}

	return sign_ptr;
}
/**************************************************************************************************************************/
