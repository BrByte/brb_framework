/*
 * md5.c
 *
 *  Created on: 2008-10-01
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *      Author: Henrique Fernandes Silveira
 *
 *
 * NO COPYRIGHT - THIS IS 100% IN THE PUBLIC DOMAIN
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

#include "libbrb_data.h"

/**************************************************************************************************************************/
void BRB_MD5Init(BRB_MD5_CTX *ctx)
{
	ctx->buf[0]		= 0x67452301;
	ctx->buf[1]		= 0xefcdab89;
	ctx->buf[2]		= 0x98badcfe;
	ctx->buf[3]		= 0x10325476;

	ctx->bytes[0]	= 0;
	ctx->bytes[1]	= 0;
}
/**************************************************************************************************************************/
void BRB_MD5UpdateBig(BRB_MD5_CTX *ctx, const void *_buf, unsigned long len)
{
	char *cur_base				= (char*)_buf;
	unsigned int digest_chunk	= 65535;

	/* Digest data in CHUNKs */
	while (len >= digest_chunk)
	{
		BRB_MD5Update(ctx, cur_base, digest_chunk);

		cur_base	+= digest_chunk;
		len			-= digest_chunk;

		continue;
	}

	/* Digest LAST */
	if (len > 0)
		BRB_MD5Update(ctx, cur_base, len);

	return;
}
/**************************************************************************************************************************/
void BRB_MD5Update(BRB_MD5_CTX *ctx, const void *_buf, unsigned long len)
{
	uint8_t const *buf = _buf;
	unsigned long t;

	/* Update byte count */
	t = ctx->bytes[0];

	if ((ctx->bytes[0] = t + len) < t)
		ctx->bytes[1]++;	/* Carry from low to high */

	t = 64 - (t & 0x3f);	/* Space available in ctx->in (at least 1) */

	if (t > len)
	{
		memcpy((uint8_t *) ctx->in + 64 - t, buf, len);
		return;
	}

	/* First chunk is an odd size */
	memcpy((uint8_t *) ctx->in + 64 - t, buf, t);
	BRB_MD5Transform(ctx);
	buf += t;
	len -= t;

	/* Process data in 64-byte chunks */
	while (len >= 64)
	{
		memcpy(ctx->in, buf, 64);
		BRB_MD5Transform(ctx);
		buf += 64;
		len -= 64;

		continue;
	}

	/* Handle any remaining bytes of data. */
	memcpy(ctx->in, buf, len);
}
/**************************************************************************************************************************/
void BRB_MD5UpdateLowerText(BRB_MD5_CTX *md5_context, char *key_ptr, int key_sz)
{
	char tmp_buff[128] = {0};
	int i;

	/* Sanitize */
	if (!key_ptr || key_sz <= 0)
		return;

	for (i = 0; i < key_sz; i++)
	{
		if (key_ptr[i] >= 'A' && key_ptr[i] <= 'Z')
			tmp_buff[i] 	= key_ptr[i] + 32;
		else
			tmp_buff[i] 	= key_ptr[i];
	}

	BRB_MD5Update(md5_context, (char *)&tmp_buff, key_sz);

	return;
}
/**************************************************************************************************************************/
void BRB_MD5Final(BRB_MD5_CTX *ctx)
{
	/* Number of bytes in ctx->in */
	int count	= ctx->bytes[0] & 0x3f;
	uint8_t *p	= (uint8_t *) ctx->in + count;

	/* Set the first char of padding to 0x80.  There is always room. */
	*p++ = 0x80;

	/* Bytes of padding needed to make 56 bytes (-8..55) */
	count = 56 - 1 - count;

	/* Padding forces an extra block */
	if (count < 0)
	{
		memset(p, 0, count + 8);
		BRB_MD5Transform(ctx);
		p = (uint8_t *) ctx->in;
		count = 56;
	}

	memset(p, 0, count);

	/* Append length in bits and transform */
	ctx->in[14] = ctx->bytes[0] << 3;
	ctx->in[15] = ctx->bytes[1] << 3 | ctx->bytes[0] >> 29;
	BRB_MD5Transform(ctx);

	memcpy(&ctx->digest, ctx->buf, 16);

	/* Initialize string */
	BRB_MD5LateInitDigestString(ctx);

	return;
}
/**************************************************************************************************************************/
void BRB_MD5Transform(BRB_MD5_CTX *ctx)
{
	unsigned int a, b, c, d;

	a = ctx->buf[0];
	b = ctx->buf[1];
	c = ctx->buf[2];
	d = ctx->buf[3];

	MD5STEP(F1, a, b, c, d, ctx->in[0] + 0xd76aa478, 7);
	MD5STEP(F1, d, a, b, c, ctx->in[1] + 0xe8c7b756, 12);
	MD5STEP(F1, c, d, a, b, ctx->in[2] + 0x242070db, 17);
	MD5STEP(F1, b, c, d, a, ctx->in[3] + 0xc1bdceee, 22);
	MD5STEP(F1, a, b, c, d, ctx->in[4] + 0xf57c0faf, 7);
	MD5STEP(F1, d, a, b, c, ctx->in[5] + 0x4787c62a, 12);
	MD5STEP(F1, c, d, a, b, ctx->in[6] + 0xa8304613, 17);
	MD5STEP(F1, b, c, d, a, ctx->in[7] + 0xfd469501, 22);
	MD5STEP(F1, a, b, c, d, ctx->in[8] + 0x698098d8, 7);
	MD5STEP(F1, d, a, b, c, ctx->in[9] + 0x8b44f7af, 12);
	MD5STEP(F1, c, d, a, b, ctx->in[10] + 0xffff5bb1, 17);
	MD5STEP(F1, b, c, d, a, ctx->in[11] + 0x895cd7be, 22);
	MD5STEP(F1, a, b, c, d, ctx->in[12] + 0x6b901122, 7);
	MD5STEP(F1, d, a, b, c, ctx->in[13] + 0xfd987193, 12);
	MD5STEP(F1, c, d, a, b, ctx->in[14] + 0xa679438e, 17);
	MD5STEP(F1, b, c, d, a, ctx->in[15] + 0x49b40821, 22);

	MD5STEP(F2, a, b, c, d, ctx->in[1] + 0xf61e2562, 5);
	MD5STEP(F2, d, a, b, c, ctx->in[6] + 0xc040b340, 9);
	MD5STEP(F2, c, d, a, b, ctx->in[11] + 0x265e5a51, 14);
	MD5STEP(F2, b, c, d, a, ctx->in[0] + 0xe9b6c7aa, 20);
	MD5STEP(F2, a, b, c, d, ctx->in[5] + 0xd62f105d, 5);
	MD5STEP(F2, d, a, b, c, ctx->in[10] + 0x02441453, 9);
	MD5STEP(F2, c, d, a, b, ctx->in[15] + 0xd8a1e681, 14);
	MD5STEP(F2, b, c, d, a, ctx->in[4] + 0xe7d3fbc8, 20);
	MD5STEP(F2, a, b, c, d, ctx->in[9] + 0x21e1cde6, 5);
	MD5STEP(F2, d, a, b, c, ctx->in[14] + 0xc33707d6, 9);
	MD5STEP(F2, c, d, a, b, ctx->in[3] + 0xf4d50d87, 14);
	MD5STEP(F2, b, c, d, a, ctx->in[8] + 0x455a14ed, 20);
	MD5STEP(F2, a, b, c, d, ctx->in[13] + 0xa9e3e905, 5);
	MD5STEP(F2, d, a, b, c, ctx->in[2] + 0xfcefa3f8, 9);
	MD5STEP(F2, c, d, a, b, ctx->in[7] + 0x676f02d9, 14);
	MD5STEP(F2, b, c, d, a, ctx->in[12] + 0x8d2a4c8a, 20);

	MD5STEP(F3, a, b, c, d, ctx->in[5] + 0xfffa3942, 4);
	MD5STEP(F3, d, a, b, c, ctx->in[8] + 0x8771f681, 11);
	MD5STEP(F3, c, d, a, b, ctx->in[11] + 0x6d9d6122, 16);
	MD5STEP(F3, b, c, d, a, ctx->in[14] + 0xfde5380c, 23);
	MD5STEP(F3, a, b, c, d, ctx->in[1] + 0xa4beea44, 4);
	MD5STEP(F3, d, a, b, c, ctx->in[4] + 0x4bdecfa9, 11);
	MD5STEP(F3, c, d, a, b, ctx->in[7] + 0xf6bb4b60, 16);
	MD5STEP(F3, b, c, d, a, ctx->in[10] + 0xbebfbc70, 23);
	MD5STEP(F3, a, b, c, d, ctx->in[13] + 0x289b7ec6, 4);
	MD5STEP(F3, d, a, b, c, ctx->in[0] + 0xeaa127fa, 11);
	MD5STEP(F3, c, d, a, b, ctx->in[3] + 0xd4ef3085, 16);
	MD5STEP(F3, b, c, d, a, ctx->in[6] + 0x04881d05, 23);
	MD5STEP(F3, a, b, c, d, ctx->in[9] + 0xd9d4d039, 4);
	MD5STEP(F3, d, a, b, c, ctx->in[12] + 0xe6db99e5, 11);
	MD5STEP(F3, c, d, a, b, ctx->in[15] + 0x1fa27cf8, 16);
	MD5STEP(F3, b, c, d, a, ctx->in[2] + 0xc4ac5665, 23);

	MD5STEP(F4, a, b, c, d, ctx->in[0] + 0xf4292244, 6);
	MD5STEP(F4, d, a, b, c, ctx->in[7] + 0x432aff97, 10);
	MD5STEP(F4, c, d, a, b, ctx->in[14] + 0xab9423a7, 15);
	MD5STEP(F4, b, c, d, a, ctx->in[5] + 0xfc93a039, 21);
	MD5STEP(F4, a, b, c, d, ctx->in[12] + 0x655b59c3, 6);
	MD5STEP(F4, d, a, b, c, ctx->in[3] + 0x8f0ccc92, 10);
	MD5STEP(F4, c, d, a, b, ctx->in[10] + 0xffeff47d, 15);
	MD5STEP(F4, b, c, d, a, ctx->in[1] + 0x85845dd1, 21);
	MD5STEP(F4, a, b, c, d, ctx->in[8] + 0x6fa87e4f, 6);
	MD5STEP(F4, d, a, b, c, ctx->in[15] + 0xfe2ce6e0, 10);
	MD5STEP(F4, c, d, a, b, ctx->in[6] + 0xa3014314, 15);
	MD5STEP(F4, b, c, d, a, ctx->in[13] + 0x4e0811a1, 21);
	MD5STEP(F4, a, b, c, d, ctx->in[4] + 0xf7537e82, 6);
	MD5STEP(F4, d, a, b, c, ctx->in[11] + 0xbd3af235, 10);
	MD5STEP(F4, c, d, a, b, ctx->in[2] + 0x2ad7d2bb, 15);
	MD5STEP(F4, b, c, d, a, ctx->in[9] + 0xeb86d391, 21);

	ctx->buf[0] += a;
	ctx->buf[1] += b;
	ctx->buf[2] += c;
	ctx->buf[3] += d;

	return;
}
/**************************************************************************************************************************/
void BRB_MD5LateInitDigestString(BRB_MD5_CTX *ret )
{
        sprintf((void*)&ret->string, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
                        ret->digest[0], ret->digest[1], ret->digest[2], ret->digest[3], ret->digest[4], ret->digest[5], ret->digest[6],
                        ret->digest[7], ret->digest[8], ret->digest[9], ret->digest[10], ret->digest[11], ret->digest[12], ret->digest[13],
                        ret->digest[14], ret->digest[15]);
        return;
}
/**************************************************************************************************************************/
void BRB_MD5ToStr(unsigned char *bin_digest,  unsigned char *ret_buf_str)
{
        sprintf((void*)ret_buf_str, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
                        bin_digest[0], bin_digest[1], bin_digest[2], bin_digest[3], bin_digest[4], bin_digest[5],
                        bin_digest[6], bin_digest[7], bin_digest[8], bin_digest[9], bin_digest[10], bin_digest[11],
                        bin_digest[12], bin_digest[13], bin_digest[14], bin_digest[15]);
        return;
}
/**************************************************************************************************************************/
