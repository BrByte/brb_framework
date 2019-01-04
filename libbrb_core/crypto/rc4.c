/*
 * rc4.c
 *
 *  Created on: 2014-10-31
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

#include "libbrb_data.h"

static void BRB_RC4_SwapBytes(unsigned char *a, unsigned char *b);

/**************************************************************************************************************************/
void BRB_RC4_Init(BRB_RC4_State *state, const unsigned char *key, int keylen)
{
	unsigned char j;
	int i;

	/* Initialize state with identity permutation */
	for (i = 0; i < 256; i++)
		state->perm[i] = (unsigned char)i;

	state->index1 = 0;
	state->index2 = 0;

	/* Randomize the permutation using key data */
	for (j = i = 0; i < 256; i++)
	{
		j += state->perm[i] + key[i % keylen];
		BRB_RC4_SwapBytes(&state->perm[i], &state->perm[j]);

		continue;
	}

	return;
}
/**************************************************************************************************************************/
void BRB_RC4_Crypt(BRB_RC4_State *state, const unsigned char *inbuf, unsigned char *outbuf, int buflen)
{
	unsigned char j;
	int i;

	for (i = 0; i < buflen; i++)
	{

		/* Update modification indexes */
		state->index1++;
		state->index2 += state->perm[state->index1];

		/* Modify permutation */
		BRB_RC4_SwapBytes(&state->perm[state->index1], &state->perm[state->index2]);

		/* Encrypt/decrypt next byte */
		j = state->perm[state->index1] + state->perm[state->index2];
		outbuf[i] = inbuf[i] ^ state->perm[j];

		continue;
	}

	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void BRB_RC4_SwapBytes(unsigned char *a, unsigned char *b)
{
	unsigned char temp;

	temp	= *a;
	*a		= *b;
	*b		= temp;

	return;
}
/**************************************************************************************************************************/

