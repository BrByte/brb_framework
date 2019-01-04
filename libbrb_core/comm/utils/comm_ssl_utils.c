/*
 * comm_ssl_utils.c
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

#include "../include/libbrb_ev_kq.h"

/**************************************************************************************************************************/
X509 *CommEvSSLUtils_X509ForgeAndSignFromConnHnd(CommEvTCPServerConn *conn_hnd, int valid_sec, int wildcard)
{
	CommEvTCPServerCertificate *ca_certinfo;
	X509 *forged_cert;
	char wildcard_str[512];

	/* Grab CA certificate from our listener */
	ca_certinfo	= &conn_hnd->parent_srv->cfg[conn_hnd->listener->slot_id].ssl.ca_cert;

	/* Forge a new X.509 certificate on the fly for this connection based exactly on SNI information */
	if (!wildcard)
	{
		/* Forge */
		forged_cert = CommEvSSLUtils_X509ForgeAndSignFromParams(ca_certinfo->x509_cert, ca_certinfo->key_private, conn_hnd->parent_srv->ssldata.main_key,
				CommEvTCPServerConnSSLDataGetSNIStr(conn_hnd), valid_sec);

	}
	/* Master wants a WILDCARDED certificate, do it */
	else
	{
		/* Extract top level domain */
		conn_hnd->ssldata.sni_host_tldpos = CommEvSSLUtils_GenerateWildCardFromDomain(CommEvTCPServerConnSSLDataGetSNIStr(conn_hnd), (char*)&wildcard_str, (sizeof(wildcard_str)));
		//printf("CommEvSSLUtils_X509ForgeAndSignFromConnHnd - WILDCARD [%s] - TLD POS [%d]\n", wildcard_str, conn_hnd->ssldata.sni_host_tldpos);

		/* Forge WILDCARD */
		if (conn_hnd->ssldata.sni_host_tldpos > 0)
		{
			forged_cert = CommEvSSLUtils_X509ForgeAndSignFromParams(ca_certinfo->x509_cert, ca_certinfo->key_private, conn_hnd->parent_srv->ssldata.main_key, (char*)&wildcard_str, valid_sec);
			conn_hnd->ssldata.sni_host_tldpos++;
		}
		/* Non WILDCARDable domain, forge by exact SNI */
		else
		{
			forged_cert = CommEvSSLUtils_X509ForgeAndSignFromParams(ca_certinfo->x509_cert, ca_certinfo->key_private, conn_hnd->parent_srv->ssldata.main_key,
					CommEvTCPServerConnSSLDataGetSNIStr(conn_hnd), valid_sec);
		}
	}

	return forged_cert;
}
/**************************************************************************************************************************/
X509 *CommEvSSLUtils_X509ForgeAndSignFromParams(X509 *ca_cert, EVP_PKEY *cakey, EVP_PKEY *key, const char *dnsname_str, long valid_sec)
{
	X509V3_CTX ctx;
	X509_NAME *subject, *issuer;
	GENERAL_NAMES *names;
	GENERAL_NAME *gn;
	X509 *forged_cert;
	char dns_name_buf[512];

	unsigned int random_serial	= arc4random();

	subject	= X509_get_subject_name(ca_cert /* orig */);
	issuer	= X509_get_subject_name(ca_cert);

	if (!subject || !issuer)
		return NULL;

	forged_cert = X509_new();

	if (!forged_cert)
		return NULL;

	/* Set some values into certificate */
	if (!X509_set_version(forged_cert, 0x02) || !X509_set_subject_name(forged_cert, subject) || !X509_set_issuer_name(forged_cert, issuer)
			|| !ASN1_INTEGER_set(X509_get_serialNumber (forged_cert), random_serial) ||	!X509_gmtime_adj(X509_get_notBefore(forged_cert), (long)-60*60*24)
			|| !X509_gmtime_adj(X509_get_notAfter(forged_cert), valid_sec) || !X509_set_pubkey(forged_cert, key))
		goto errout;

	/* add standard v3 extensions; cf. RFC 2459 */
	X509V3_set_ctx(&ctx, ca_cert, forged_cert, NULL, NULL, 0);

	if (CommEvSSLUtils_X509V3ExtAdd(&ctx, forged_cert, "basicConstraints", "CA:FALSE") == -1 ||
			CommEvSSLUtils_X509V3ExtAdd(&ctx, forged_cert, "keyUsage", "digitalSignature," "keyEncipherment") == -1 ||
			CommEvSSLUtils_X509V3ExtAdd(&ctx, forged_cert, "extendedKeyUsage", "serverAuth") == -1 ||
			CommEvSSLUtils_X509V3ExtAdd(&ctx, forged_cert, "subjectKeyIdentifier", "hash") == -1 ||
			CommEvSSLUtils_X509V3ExtAdd(&ctx, forged_cert, "authorityKeyIdentifier", "keyid,issuer:always") == -1)
		goto errout;


	CommEvSSLUtils_X509V3ExtAdd(&ctx, forged_cert, "nsComment", "Generated by Speedr Webcache from BrByte");

	/* Generate DNS_NAME for this certificate */
	snprintf((char*)&dns_name_buf, sizeof(dns_name_buf), "DNS:%s", dnsname_str);

	if (CommEvSSLUtils_X509V3ExtAdd(&ctx, forged_cert, "subjectAltName", dns_name_buf) == -1)
		goto errout;

	/* Sign the new forged certificate */
	if (!CommEvSSLUtils_X509CertSign(forged_cert, cakey))
		goto errout;

	/* Return the certificate (or NULL on failure) */
	return forged_cert;

	errout3:
	GENERAL_NAME_free(gn);
	errout2:
	sk_GENERAL_NAME_pop_free(names, GENERAL_NAME_free);
	errout:
	X509_free(forged_cert);
	return NULL;
}
/**************************************************************************************************************************/
StringArray *CommEvSSLUtils_X509AltNamesToStringArray(X509 *cert)
{
	const GENERAL_NAME *current_name;
	StringArray *altname_strarr;
	char *current_dns_name;
	int i;

	int san_names_nb = -1;
	STACK_OF(GENERAL_NAME) *san_names = NULL;

	/* Try to extract the names within the SAN (Subjbect Alternative Names) extension from the certificate */
	san_names = X509_get_ext_d2i((X509 *)cert, NID_subject_alt_name, NULL, NULL);

	/* No alternative names on this certificate */
	if (NULL == san_names)
		return NULL;

	 san_names_nb = sk_GENERAL_NAME_num(san_names);

	/* Create a new string array for our alternative names */
	altname_strarr = StringArrayNew(BRBDATA_THREAD_UNSAFE, 8);

	/* Check each name within the extension */
	for (i = 0; i < san_names_nb; i++)
	{
		current_name = sk_GENERAL_NAME_value(san_names, i);

		/* Current name is a DNS name, let's check it */
		if (current_name->type == GEN_DNS)
		{
			current_dns_name = (char *) ASN1_STRING_data(current_name->d.dNSName);

			/* Make sure there isn't an embedded NULL character in the DNS name - MALFORMED CERTIFICATE */
			if (ASN1_STRING_length(current_name->d.dNSName) != strlen(current_dns_name))
				break;

			/* Add name into string array */
			StringArrayAdd(altname_strarr, current_dns_name);
		}

		continue;
	}

	/* Free Subject alternative names */
	sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);

	return altname_strarr;
}
/**************************************************************************************************************************/
X509 *CommEvSSLUtils_X509ForgeAndSignFromOrigCert(X509 *ca_cert, EVP_PKEY *cakey, X509 *origcrt, const char *dnsname_str, EVP_PKEY *key, long valid_sec)
{
	X509_EXTENSION *ext;
	X509V3_CTX ctx;
	X509_NAME *subject;
	X509_NAME *issuer;
	GENERAL_NAMES *names;
	GENERAL_NAME *gn;
	X509 *forged_cert;

	subject	= X509_get_subject_name(origcrt);
	issuer	= X509_get_subject_name(ca_cert);

	if (!subject || !issuer)
		return NULL;

	forged_cert = X509_new();

	if (!forged_cert)
		return NULL;

	if (!X509_set_version(forged_cert, 0x02) || !X509_set_subject_name(forged_cert, subject) || !X509_set_issuer_name(forged_cert, issuer) || CommEvSSLUtils_X509CopyRandom(forged_cert, origcrt) == -1 ||
			!X509_gmtime_adj(X509_get_notBefore(forged_cert), (long)-60*60*24*360) || !X509_gmtime_adj(X509_get_notAfter(forged_cert), valid_sec) || !X509_set_pubkey(forged_cert, key))
		goto errout;

	/* add standard v3 extensions; cf. RFC 2459 */
	X509V3_set_ctx(&ctx, ca_cert, forged_cert, NULL, NULL, 0);

	if (CommEvSSLUtils_X509V3ExtAdd(&ctx, forged_cert, "basicConstraints", "CA:FALSE") == -1 || CommEvSSLUtils_X509V3ExtAdd(&ctx, forged_cert, "keyUsage", "digitalSignature," "keyEncipherment") == -1 ||
			CommEvSSLUtils_X509V3ExtAdd(&ctx, forged_cert, "extendedKeyUsage", "serverAuth") == -1 || CommEvSSLUtils_X509V3ExtAdd(&ctx, forged_cert, "subjectKeyIdentifier", "hash") == -1 ||
			CommEvSSLUtils_X509V3ExtAdd(&ctx, forged_cert, "authorityKeyIdentifier", "keyid,issuer:always") == -1)
		goto errout;


	/* no extraname provided: copy original subjectAltName ext */
	if (!dnsname_str)
	{
		if (CommEvSSLUtils_X509V3ExtCopyByNID(forged_cert, origcrt, NID_subject_alt_name) == -1)
			goto errout;
	}
	else
	{
		names = X509_get_ext_d2i(origcrt, NID_subject_alt_name, 0, 0);

		/* no subjectAltName present: add new one */
		if (!names)
		{
			char *cfval;

			if (asprintf(&cfval, "DNS:%s", dnsname_str) == -1)
				goto errout;

			if (CommEvSSLUtils_X509V3ExtAdd(&ctx, forged_cert, "subjectAltName", cfval) == -1)
			{
				free(cfval);
				goto errout;
			}

			free(cfval);
		}
		/* add extraname to original subjectAltName and add it to the new certificate */
		else
		{
			/* Create a new GENERAL name */
			gn = GENERAL_NAME_new();

			/* Failed allocating, bail out */
			if (!gn)
				goto errout2;

			/* Set it to correct type and initialize */
			gn->type		= GEN_DNS;
			gn->d.dNSName	= M_ASN1_IA5STRING_new();

			/* Failed allocating, bail out */
			if (!gn->d.dNSName)
				goto errout3;

			/* Set it to user defined DNS name, and push it into general names */
			ASN1_STRING_set(gn->d.dNSName, (unsigned char *)dnsname_str, strlen(dnsname_str));
			sk_GENERAL_NAME_push(names, gn);

			/* Create NID with general names */
			ext = X509V3_EXT_i2d(NID_subject_alt_name, 0, names);

			if (!X509_add_ext(forged_cert, ext, -1))
			{
				if (ext)
				{
					X509_EXTENSION_free(ext);
				}
				goto errout3;
			}

			X509_EXTENSION_free(ext);
			sk_GENERAL_NAME_pop_free(names, GENERAL_NAME_free);
		}
	}

	//ssl_x509_v3ext_add(&ctx, forged_cert, "nsComment", "Generated by " PNAME);

	/* Sign the new forged certificate */
	if (!CommEvSSLUtils_X509CertSign(forged_cert, cakey))
		goto errout;

	return forged_cert;

	errout3:
	GENERAL_NAME_free(gn);
	errout2:
	sk_GENERAL_NAME_pop_free(names, GENERAL_NAME_free);
	errout:
	X509_free(forged_cert);
	return NULL;
}
/**************************************************************************************************************************/
int CommEvSSLUtils_X509CertSign(X509 *target_cert, EVP_PKEY *cakey)
{
	const EVP_MD *md;

	switch (EVP_PKEY_type(cakey->type))
	{
	case EVP_PKEY_RSA:
		md = EVP_sha512();
		break;
	case EVP_PKEY_DSA:
		md = EVP_dss1();
		break;
	case EVP_PKEY_EC:
		md = EVP_ecdsa();
		break;
	default:
		return 0;
	}

	if (!X509_sign(target_cert, cakey, md))
		return 0;

	return 1;

}
/**************************************************************************************************************************/
void CommEvSSLUtils_X509CertRefCountInc(X509 *crt, int thread_safe)
{
	if (thread_safe)
		CRYPTO_add(&crt->references, 1, CRYPTO_LOCK_X509);
	else
		crt->references++;

	return;
}
/**************************************************************************************************************************/
void CommEvSSLUtils_X509PrivateKeyRefCountInc(EVP_PKEY *key, int thread_safe)
{
	if (thread_safe)
		CRYPTO_add(&key->references, 1, CRYPTO_LOCK_EVP_PKEY);
	else
		key->references++;

	return;
}
/**************************************************************************************************************************/
int CommEvSSLUtils_X509CopyRandom(X509 *dstcrt, X509 *srccrt)
{
	ASN1_INTEGER *srcptr, *dstptr;
	BIGNUM *bnserial;
	unsigned int rand;
	int rv;

	rv = CommEvSSLUtils_Random(&rand, sizeof(rand));

	dstptr = X509_get_serialNumber(dstcrt);
	srcptr = X509_get_serialNumber(srccrt);

	if ((rv == -1) || !dstptr || !srcptr)
		return -1;
	bnserial = ASN1_INTEGER_to_BN(srcptr, NULL);

	if (!bnserial)
	{
		/* random 32-bit serial */
		ASN1_INTEGER_set(dstptr, rand);
	}
	else
	{
		/* original serial plus random 32-bit offset */
		BN_add_word(bnserial, rand);
		BN_to_ASN1_INTEGER(bnserial, dstptr);
		BN_free(bnserial);
	}
	return 0;
}
/**************************************************************************************************************************/
int CommEvSSLUtils_X509V3ExtAdd(X509V3_CTX *ctx, X509 *crt, char *k, char *v)
{
	X509_EXTENSION *ext;

	if (!(ext = X509V3_EXT_conf(NULL, ctx, k, v))) {
		return -1;
	}
	if (X509_add_ext(crt, ext, -1) != 1) {
		X509_EXTENSION_free(ext);
		return -1;
	}
	X509_EXTENSION_free(ext);
	return 0;
}
/**************************************************************************************************************************/
int CommEvSSLUtils_X509V3ExtCopyByNID(X509 *crt, X509 *origcrt, int nid)
{
	X509_EXTENSION *ext;
	int pos;

	pos = X509_get_ext_by_NID(origcrt, nid, -1);
	if (pos == -1)
		return 0;
	ext = X509_get_ext(origcrt, pos);
	if (!ext)
		return -1;
	if (X509_add_ext(crt, ext, -1) != 1)
		return -1;
	return 1;
}
/**************************************************************************************************************************/
int CommEvSSLUtils_Random(void *p, unsigned long sz)
{
	int rv;

	rv = RAND_pseudo_bytes((unsigned char*)p, sz);
	if (rv == -1) {
		rv = RAND_bytes((unsigned char*)p, sz);
		if (rv != 1)
			return -1;
	}
	return 0;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
X509 *CommEvSSLUtils_X509CertFromPEM(char *pem_str, int pem_strsz)
{
	BIO *bio;
	X509 *cert;

	bio = BIO_new(BIO_s_mem());

	/* Load PEM string into BIO */
	//BIO_write(bio,(const void*)pem_str, pem_strsz);
	BIO_puts(bio, pem_str);

	cert = PEM_read_bio_X509(bio,NULL,0,NULL);

	BIO_free(bio);

	return cert;
}
/**************************************************************************************************************************/
void CommEvSSLUtils_X509CertToStr(X509 *crt, char *ret_buf, int ret_buf_maxsz)
{
	BIO *bio;
	char *p, *ret;
	size_t sz;

	/* NULL terminate return buffer */
	memset(ret_buf, 0, ret_buf_maxsz);

	bio = BIO_new(BIO_s_mem());

	if (!bio)
		return;

	X509_print(bio, crt);
	sz = BIO_get_mem_data(bio, &p);

	/* Copy PEM string into destination buffer */
	strlcpy(ret_buf, p, sz);
	ret_buf[sz] = '\0';

	BIO_free(bio);

	return;
}
/**************************************************************************************************************************/
void CommEvSSLUtils_X509CertToPEM(X509 *crt, char *ret_buf, int ret_buf_maxsz)
{
	BIO *bio;
	char *p; //*ret;
	size_t sz;

	/* NULL terminate return buffer */
	memset(ret_buf, 0, ret_buf_maxsz);

	bio = BIO_new(BIO_s_mem());

	if (!bio)
		return;

	PEM_write_bio_X509(bio, crt);
	sz = BIO_get_mem_data(bio, &p);

	/* Copy PEM string into destination buffer */
	strlcpy(ret_buf, p, sz);
	ret_buf[sz] = '\0';

	BIO_free(bio);

	return;
}
/**************************************************************************************************************************/
X509 *CommEvSSLUtils_X509CertFromFile(const char *filename)
{
	FILE *file_fp;
	int op_status;
	X509 *cert = NULL;

	/* Try to open file for writing */
	file_fp = fopen (filename, "r");

	/* Failed to open file for writing, bail out */
	if (!file_fp)
		return NULL;

	cert = PEM_read_X509(file_fp, NULL, NULL, NULL);

	/* Close */
	fclose(file_fp);

	return cert;
}
/**************************************************************************************************************************/
int CommEvSSLUtils_X509CertToFile(const char *filename, X509 *cert)
{
	FILE *file_fp;
	int op_status;

	/* Try to open file for writing */
	file_fp = fopen (filename, "w+");

	/* Failed to open file for writing, bail out */
	if (!file_fp)
		return 0;

	PEM_write_X509(file_fp, cert);

	/* Flush and close */
	fflush(file_fp);
	fclose(file_fp);

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
EVP_PKEY *CommEvSSLUtils_X509PrivateKeyFromPEM(char *pem_str, int pem_strsz)
{
	BIO *bio;
	EVP_PKEY *priv_key;

	bio = BIO_new(BIO_s_mem());

	/* Load PEM string into BIO */
	BIO_puts(bio, pem_str);

	priv_key = PEM_read_bio_PrivateKey(bio, NULL, 0, NULL);

	BIO_free(bio);

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

	bio = BIO_new(BIO_s_mem());

	if (!bio)
		return;

	PEM_write_bio_PrivateKey(bio, key, NULL, NULL, 0, NULL, NULL);
	sz = BIO_get_mem_data(bio, &p);

	/* Copy PEM string into destination buffer */
	strlcpy(ret_buf, p, sz);
	ret_buf[sz] = '\0';

	BIO_free(bio);

	return;
}
/**************************************************************************************************************************/
int CommEvSSLUtils_X509PrivateKeyWriteToFile(const char *filename, EVP_PKEY *key)
{
	FILE *file_fp;
	int op_status;

	/* Try to open file for writing */
	file_fp = fopen(filename, "w+");

	/* Failed to open file for writing, bail out */
	if (!file_fp)
		return 0;

	op_status = PEM_write_PrivateKey(file_fp, key, NULL, NULL, 0, 0, NULL);

	/* Failed writing, bail out */
	if (op_status != 1)
	{
		fclose(file_fp);
		return 0;
	}

	/* Flush and close */
	fflush(file_fp);
	fclose(file_fp);

	return 1;
}
/**************************************************************************************************************************/
EVP_PKEY *CommEvSSLUtils_X509PrivateKeyReadFromFile(const char *filename)
{
	EVP_PKEY *key;
	FILE *file_fp;
	int op_status;

	/* Try to open file for writing */
	file_fp = fopen (filename, "r");

	/* Failed to open file for writing, bail out */
	if (!file_fp)
		return 0;

	key	= EVP_PKEY_new();
	PEM_read_PrivateKey(file_fp, &key, NULL, NULL);

	/* Close */
	fclose(file_fp);

	return key;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
EVP_PKEY *CommEvSSLUtils_X509PublicKeyFromPEM(char *pem_str, int pem_strsz)
{
	BIO *bio;
	EVP_PKEY *priv_key;

	bio = BIO_new(BIO_s_mem());

	/* Load PEM string into BIO */
	BIO_puts(bio, pem_str);

	priv_key = PEM_read_bio_PUBKEY(bio, NULL, 0, NULL);

	BIO_free(bio);

	return priv_key;
}
/**************************************************************************************************************************/
void CommEvSSLUtils_X509PublicKeyToPEM(EVP_PKEY *key, char *ret_buf, int ret_buf_maxsz)
{
	BIO *bio;
	char *p, *ret;
	size_t sz;

	/* NULL terminate return buffer */
	memset(ret_buf, 0, ret_buf_maxsz);


	bio = BIO_new(BIO_s_mem());

	if (!bio)
		return;

	PEM_write_bio_PUBKEY(bio, key);
	sz = BIO_get_mem_data(bio, &p);

	/* Copy PEM string into destination buffer */
	strlcpy(ret_buf, p, sz);
	ret_buf[sz] = '\0';

	BIO_free(bio);

	return;
}
/**************************************************************************************************************************/
int CommEvSSLUtils_X509PublicKeyWriteToFile(const char *filename, EVP_PKEY *key)
{
	FILE *file_fp;
	int op_status;

	/* Try to open file for writing */
	file_fp = fopen(filename, "w+");

	/* Failed to open file for writing, bail out */
	if (!file_fp)
		return 0;

	op_status = PEM_write_PUBKEY(file_fp, key);

	/* Failed writing, bail out */
	if (op_status != 1)
	{
		fclose(file_fp);
		return 0;
	}

	/* Flush and close */
	fflush(file_fp);
	fclose(file_fp);

	return 1;
}
/**************************************************************************************************************************/
EVP_PKEY *CommEvSSLUtils_X509PublicKeyReadFromFile(const char *filename)
{
	EVP_PKEY *key;
	FILE *file_fp;
	int op_status;

	/* Try to open file for writing */
	file_fp = fopen (filename, "r");

	/* Failed to open file for writing, bail out */
	if (!file_fp)
		return 0;

	key	= EVP_PKEY_new();
	PEM_read_PUBKEY(file_fp, &key, NULL, NULL);

	/* Close */
	fclose(file_fp);

	return key;
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
int CommEvSSLUtils_X509CertRootNew(CommEvSSLUtilsCertReq *cert_req)
{
	X509V3_CTX ctx;
	X509 *ret_cert							= NULL;
	EVP_PKEY *private_key					= NULL;
	X509_NAME *cert_name					= NULL;
	RSA *key_pair							= NULL;
	BIGNUM *big_number						= NULL;
	int op_status							= 0;

	/* Fake Loop */
	do
	{
		/* Create the certificate object */
		ret_cert = X509_new();

		if (!ret_cert)
		{
			printf("CommEvSSLUtils_X509CertNew - X509_new() failed\n");
			break;
		}

		// Set version 2, and get version 3
		X509_set_version (ret_cert, 2);

		// Set the certificate's properties
		ASN1_INTEGER_set (X509_get_serialNumber (ret_cert), cert_req->options.serial);
		X509_gmtime_adj (X509_get_notBefore (ret_cert), (long)-60*60*24*360);
		X509_gmtime_adj (X509_get_notAfter (ret_cert), (long)(60 * 60 * 24 * (cert_req->options.valid_days ? cert_req->options.valid_days : 1)));
		cert_name = X509_get_subject_name (ret_cert);

		//C = country		ST = state		L = locality		O = organisation		OU = organisational unit		CN = common name

		if (cert_req->options.country_str && *cert_req->options.country_str)
			X509_NAME_add_entry_by_txt (cert_name, "C", MBSTRING_ASC, (unsigned char*)cert_req->options.country_str, -1, -1, 0);

		if (cert_req->options.common_name_str && *cert_req->options.common_name_str)
			X509_NAME_add_entry_by_txt (cert_name, "CN", MBSTRING_ASC, (unsigned char*)cert_req->options.common_name_str, -1, -1, 0);

		if (cert_req->options.organization_str && *cert_req->options.organization_str)
			X509_NAME_add_entry_by_txt (cert_name, "O", MBSTRING_ASC, (unsigned char*)cert_req->options.organization_str, -1, -1, 0);

		/* Set ISSUER */
		X509_set_issuer_name (ret_cert, cert_name);

		/* add standard v3 extensions; cf. RFC 2459 */
		X509V3_set_ctx(&ctx, ret_cert, ret_cert, NULL, NULL, 0);
		CommEvSSLUtils_X509V3ExtAdd(&ctx, ret_cert, "basicConstraints", "CA:TRUE");
		//CommEvSSLUtils_X509V3ExtAdd(&ctx, ret_cert, "keyUsage", "digitalSignature," "keyEncipherment");
		//CommEvSSLUtils_X509V3ExtAdd(&ctx, ret_cert, "extendedKeyUsage", "serverAuth");
		//CommEvSSLUtils_X509V3ExtAdd(&ctx, ret_cert, "subjectKeyIdentifier", "hash");
		//CommEvSSLUtils_X509V3ExtAdd(&ctx, ret_cert, "authorityKeyIdentifier", "keyid,issuer:always");



		// Set the DNS name
		if (cert_req->options.dns_name_str && *cert_req->options.dns_name_str)
		{
			X509_EXTENSION *Extension;
			char Buffer[512];

			/* Format the value */
			snprintf (Buffer, sizeof(Buffer),  "DNS:%s", cert_req->options.dns_name_str);
			printf("CommEvSSLUtils_X509CertNew - Setting DNS_NAME [%s]\n", Buffer);

			//ssl_x509_v3ext_add(&ctx, crt, "subjectAltName", Buffer);

			Extension = X509V3_EXT_conf_nid (NULL, NULL, NID_subject_alt_name, Buffer);

			if (Extension)
			{
				//printf("CommEvSSLUtils_X509CertNew - Added EXT_DNS\n");
				X509_add_ext (ret_cert, Extension, -1);
				X509_EXTENSION_free (Extension);
			}
		}

		/* Create the RSA key pair object */
		key_pair = RSA_new();

		if (!key_pair)
		{
			//printf("CommEvSSLUtils_X509CertNew - RSA_generate_key() failed\n");
			break;
		}

		/* Create the big number object */
		big_number = BN_new();

		if (!big_number)
		{
			//printf("CommEvSSLUtils_X509CertNew - BN_new() failed\n");
			break;
		}

		/* Set the word */
		if (!BN_set_word (big_number, cert_req->options.exponent))
		{
			//printf("CommEvSSLUtils_X509CertNew - BN_set_word() failed\n");
			break;
		}

		/* Generate the key pair; lots of computes here */
		if (!RSA_generate_key_ex (key_pair, 1024, big_number, NULL))
		{
			//printf("CommEvSSLUtils_X509CertNew - RSA_generate_key_ex() failed\n");
			break;
		}

		/* Now we need a private key object */
		private_key = EVP_PKEY_new();

		if (!private_key)
		{
			//printf("CommEvSSLUtils_X509CertNew - EVP_PKEY_new() failed\n");
			break;
		}

		/* Assign the key pair to the private key object */
		if (!EVP_PKEY_assign_RSA (private_key, key_pair))
		{
			//printf("CommEvSSLUtils_X509CertNew - EVP_PKEY_assign_RSA() failed\n");
			break;
		}

		/* Save private key */
		cert_req->result.key_private = private_key;

		/* key_pair now belongs to private_key, so don't clean it up separately */
		key_pair = NULL;

		/* Sign it with SHA-512 */
		if (cert_req->options.self_sign)
		{
			//printf("CommEvSSLUtils_X509CertNew - Self signing new CERT\n");

			/* Set the certificate's public key from the private key object */
			if (!X509_set_pubkey (ret_cert, private_key))
			{
				//printf("CommEvSSLUtils_X509CertNew - X509_set_pubkey() failed\n");
				break;
			}

			if (!X509_sign (ret_cert, private_key, EVP_sha512()))
			{
				//printf("X509_sign - X509_sign() failed\n");
				break;
			}
		}

		/* private_key now belongs to ret_cert, so don't clean it up separately */
		private_key = NULL;

		/* op_status */
		op_status = 1;

	} while (0);

	/* Things we always clean up */
	if (big_number)
		BN_free (big_number);
	if (private_key)
		EVP_PKEY_free (private_key);

	/* Things we clean up only on failure */
	if (!op_status)
	{
		if (ret_cert)
			X509_free (ret_cert);
		if (private_key)
			EVP_PKEY_free (private_key);
		if (key_pair)
			RSA_free (key_pair);

		ret_cert = NULL;
	}

	/* Return the certificate (or NULL on failure) */
	cert_req->result.x509_cert = ret_cert;

	return 1;
}
/**************************************************************************************************************************/
int CommEvSSLUtils_GenerateWildCardFromDomain(char *src_domain, char *dst_tld_buf, int dst_tld_maxsz)
{
	int dots_found;
	int two_dots_found;
	int i, j;

	int src_domain_sz = strlen(src_domain);

	//printf("CommEvSSLUtils_GenerateWildCardFromDomain - Source domain [%s]\n", src_domain);

	/* Clean destination buffer */
	memset(dst_tld_buf, 0, dst_tld_maxsz);

	/* Generate a WILDCARD name for this certificate */
	for (i = src_domain_sz, dots_found = 0, two_dots_found = 1, j = 0; i > 0; i--, j++)
	{
		/* Seen a dot on domain */
		if ('.' == src_domain[i])
			dots_found++;

		/* Found the second dots, if we are deeper than the 7 byte, thats it, if not, keep going to the next dot */
		if ((2 == dots_found) && (j > 7) && two_dots_found)
			goto tld_found;

		/* Not found on the second dot, disable two_dots_found flag and keep moving until we hit the third dot */
		else if ((2 == dots_found) && (j == 7) && two_dots_found)
			two_dots_found = 0;

		/* Hit third dot, leave */
		if (3 == dots_found)
			goto tld_found;

		continue;

		/* Label for top level domain found */
		tld_found:

		//printf("CommEvSSLUtils_GenerateWildCardFromDomain - NON_ZERO_TLD [%s] [%d]\n", src_domain, (src_domain_sz - j));

		/* Copy reply */
		dst_tld_buf[0] = '*';
		strlcpy((char*)&dst_tld_buf[1], (src_domain + (src_domain_sz - j)), dst_tld_maxsz);

		break;
	}

	return (src_domain_sz - j);
}
/**************************************************************************************************************************/
int CommEvSSLUtils_SNIParse(const unsigned char *buf, int *sz, char *ret_buf, int retbuf_maxsz)
{
	const unsigned char *p	= buf;
	ssize_t n				= *sz;

	//printf("buffer length %zd\n", n);

	ret_buf[0] = '\0';

	if (n < 1)
	{
		*sz = -1;
		goto out;
	}

	/* first byte 0x80, third byte 0x01 is SSLv2 clientHello; - first byte 0x22, second byte 0x03 is SSLv3/TLSv1.x clientHello */
	/* record type: handshake protocol */
	if (*p != 22)
		goto out;

	p++; n--;

	if (n < 2)
	{
		*sz = -1;
		goto out;
	}

	//printf("version: %02x %02x\n", p[0], p[1]);

	if (p[0] != 3)
		goto out;

	p += 2; n -= 2;

	if (n < 2)
	{
		*sz = -1;
		goto out;
	}

	//printf("length: %02x %02x\n", p[0], p[1]);

	//#ifdef DEBUG_SNI_PARSER
	//	ssize_t recordlen = p[1] + (p[0] << 8);
	//	printf("recordlen=%zd\n", recordlen);
	//#endif /* DEBUG_SNI_PARSER */

	p += 2; n -= 2;

	if (n < 1)
	{
		*sz = -1;
		goto out;
	}

	//printf("message type: %i\n", *p);

	/* message type: ClientHello */
	if (*p != 1)
		goto out;
	p++; n--;

	if (n < 3)
	{
		*sz = -1;
		goto out;
	}

	//printf("message len: %02x %02x %02x\n", p[0], p[1], p[2]);
	ssize_t msglen = p[2] + (p[1] << 8) + (p[0] << 16);
	//printf("msglen=%zd\n", msglen);

	if (msglen < 4)
		goto out;

	p += 3; n -= 3;

	if (n < msglen)
	{
		*sz = -1;
		goto out;
	}

	n = msglen; /* only parse first message */

	if (n < 2)
		goto out;

	//printf("clienthello version %02x %02x\n", p[0], p[1]);

	if (p[0] != 3)
		goto out;

	p += 2; n -= 2;

	if (n < 32)
		goto out;

	//printf("clienthello random %02x %02x %02x %02x ...\n", p[0], p[1], p[2], p[3]);
	//printf("compare localtime: %08x\n", (unsigned int)time(NULL));

	p += 32; n -= 32;

	if (n < 1)
		goto out;

	//printf("clienthello sidlen %02x\n", *p);
	ssize_t sidlen = *p; /* session id length, 0..32 */
	p += 1; n -= 1;
	if (n < sidlen)
		goto out;
	p += sidlen; n -= sidlen;

	if (n < 2)
		goto out;
	//printf("clienthello cipher suites length %02x %02x\n", p[0], p[1]);
	ssize_t suiteslen = p[1] + (p[0] << 8);
	p += 2; n -= 2;
	if (n < suiteslen) {
		printf("n < suiteslen (%zd, %zd)\n", n, suiteslen);
		goto out;
	}
	p += suiteslen;
	n -= suiteslen;

	if (n < 1)
		goto out;
	//printf("clienthello compress methods length %02x\n", *p);
	ssize_t compslen = *p;
	p++; n--;
	if (n < compslen)
		goto out;
	p += compslen;
	n -= compslen;

	/* begin of extensions */

	if (n < 2)
		goto out;
	//printf("tlsexts length %02x %02x\n", p[0], p[1]);
	ssize_t tlsextslen = p[1] + (p[0] << 8);
	//printf("tlsextslen %zd\n", tlsextslen);
	p += 2;
	n -= 2;

	if (n < tlsextslen)
		goto out;
	n = tlsextslen; /* only parse extensions, ignore trailing bits */

	while (n > 0)
	{
		unsigned short exttype;
		ssize_t extlen;

		if (n < 4)
			goto out;

		//printf("tlsext type %02x %02x len %02x %02x\n",p[0], p[1], p[2], p[3]);
		exttype = p[1] + (p[0] << 8);
		extlen	= p[3] + (p[2] << 8);

		p += 4;
		n -= 4;

		if (n < extlen)
			goto out;

		switch (exttype)
		{
		case 0:
		{
			ssize_t extn = extlen;
			const unsigned char *extp = p;

			if (extn < 2)
				goto out;

			//printf("list length %02x %02x\n",extp[0], extp[1]);
			ssize_t namelistlen = extp[1] + (extp[0] << 8);
			//printf("namelistlen = %zd\n", namelistlen);
			extp += 2;
			extn -= 2;

			if (namelistlen != extn)
				goto out;

			while (extn > 0)
			{

				if (extn < 3)
					goto out;

				//printf("ServerName type %02x len %02x %02x\n", extp[0], extp[1], extp[2]);

				unsigned char sntype = extp[0];
				ssize_t snlen = extp[2] + (extp[1]<<8);
				extp += 3;
				extn -= 3;

				if (snlen > extn)
					goto out;
				if (snlen > TLSEXT_MAXLEN_host_name)
					goto out;

				/* deliberately not checking for malformed hostnames containing invalid chars */
				if (sntype == 0)
				{
					snlen = (snlen < retbuf_maxsz) ? snlen : retbuf_maxsz;

					memcpy(ret_buf, extp, snlen);
					ret_buf[snlen] = '\0';

					goto out;
				}

				extp += snlen;
				extn -= snlen;
			}
			break;
		}
		default:
			//printf("skipped\n");
			break;
		}
		p += extlen;
		n -= extlen;
	}

	//if (n > 0)
	//	printf("unparsed next bytes %02x %02x %02x %02x\n", p[0], p[1], p[2], p[3]);

	out:
	//printf("%zd bytes unparsed\n", n);
	return n;
}
/**************************************************************************************************************************/





//static RSA *CommEvTCPServerSSLGenerateRSACB(SSL *ssl_handle, int export, int keylen)
//{
//	static RSA *rsa_512		= NULL;
//	static RSA *rsa_1024	= NULL;
//	RSA *rsa				= NULL;
//	int newkey				= 0;
//
//	/* Generate based on KEY_LEN */
//	switch (keylen)
//	{
//	case 512:
//	{
//		if (!rsa_512)
//		{
//			rsa_512 = RSA_generate_key(512, RSA_F4, NULL, NULL);
//			newkey = 1;
//		}
//
//		rsa = rsa_512;
//		break;
//	}
//	case 1024:
//	{
//		if (!rsa_1024)
//		{
//			rsa_1024 = RSA_generate_key(1024, RSA_F4, NULL, NULL);
//			newkey = 1;
//		}
//
//		rsa = rsa_1024;
//		break;
//	}
//	default:
//		//debug(83, 1) ("CommEvTCPServerConnSSLGenerateRSACB: Unexpected key length %d\n", keylen);
//		return NULL;
//	}
//
//	if (rsa == NULL)
//	{
//		//debug(83, 1) ("CommEvTCPServerConnSSLGenerateRSACB: Failed to generate key %d\n", keylen);
//		return NULL;
//	}
//
//	//	if (newkey)
//	//	{
//	//		if (do_debug(83, 5))
//	//			PEM_write_RSAPrivateKey(debug_log, rsa, NULL, NULL, 0, NULL, NULL);
//	//		//debug(83, 1) ("CommEvTCPServerConnSSLGenerateRSACB - Generated ephemeral RSA key of length %d\n", keylen);
//	//	}
//
//	return rsa;
//}


//SSL_CTX_set_tmp_rsa_callback(srv_ptr->ssldata.ssl_context, CommEvTCPServerSSLGenerateRSACB);






//
//		/* Check if the server certificate and private-key matches */
//		if (!SSL_CTX_check_private_key(ctx))
//		{
//			fprintf(stderr,"Private key does not match the certificate public key\n");
//			exit(1);
//		}
//
//		if(verify_client == ON)
//		{
//			/* Load the RSA CA certificate into the SSL_CTX structure */
//			if (!SSL_CTX_load_verify_locations(ctx, RSA_SERVER_CA_CERT, NULL))
//			{
//				ERR_print_errors_fp(stderr);
//				exit(1);
//			}
//			/* Set to require peer (client) certificate verification */
//			SSL_CTX_set_verify(ctx,SSL_VERIFY_PEER,NULL);
//			/* Set the verification depth to 1 */
//			SSL_CTX_set_verify_depth(ctx,1);
//		}
