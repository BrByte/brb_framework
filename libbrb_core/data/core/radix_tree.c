/*
 * radix.c
 *
 *  Created on: 2012-11-18
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

#include "../include/libbrb_core.h"

static int comp_with_mask(unsigned char *addr, unsigned char *dest, unsigned int mask);
static RadixPrefix *Ref_Prefix(RadixPrefix *prefix);
static void Clear_Radix(RadixTree *radix, RadixCBType func, void *cbctx);
static RadixNode *RadixTreeSearchBest2(RadixTree *radix, RadixPrefix *prefix, int inclusive);
static void sanitise_mask(unsigned char *addr, unsigned int masklen, unsigned int maskbits);

/* WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
 * ***************************************************************
 * Note that at least one use of free in this code (RadixTreeRemove) uses the original pointer value in subsequent comparisons.
 * This may be a perfectly fine and valid coding practice but it means NULL'ing free'd pointers will break things. */


/**************************************************************************************************************************/
int RadixPrefixInit(RadixPrefix *pfx, unsigned int family, void *dest, unsigned int bitlen)
{
	memset(pfx, 0, sizeof(*pfx));

	if (family == AF_INET6)
	{
		memcpy(&pfx->add.sin6, dest, 16);
		pfx->bitlen = (bitlen >= 0) ? bitlen : 128;
		pfx->family = AF_INET6;
		pfx->ref_count = 0;
		return 1;
	}
	else if (family == AF_INET)
	{
		memcpy(&pfx->add.sin, dest, 4);
		pfx->bitlen = (bitlen >= 0) ? bitlen : 32;
		pfx->family = AF_INET;
		pfx->ref_count = 0;
		return 1;
	}
	else
		return 0;

	return 0;
}
/**************************************************************************************************************************/
RadixPrefix *RadixPrefixNew(int family, void *dest, int bitlen, RadixPrefix *prefix)
{
	int dynamic_allocated = 0;
	int default_bitlen = 32;

	if (family == AF_INET6)
	{
		default_bitlen = 128;

		if (prefix == NULL)
		{
			if ((prefix = malloc(sizeof(*prefix))) == NULL)
				return (NULL);
			memset(prefix, '\0', sizeof(*prefix));
			dynamic_allocated++;
		}

		memcpy(&prefix->add.sin6, dest, 16);

	}
	else if (family == AF_INET)
	{
		if (prefix == NULL)
		{
			if ((prefix = malloc(sizeof(*prefix))) == NULL)
				return (NULL);

			memset(prefix, '\0', sizeof(*prefix));
			dynamic_allocated++;
		}
		memcpy(&prefix->add.sin, dest, 4);
	}
	else
		return (NULL);

	prefix->bitlen = (bitlen >= 0) ? bitlen : default_bitlen;
	prefix->family = family;
	prefix->ref_count = 0;
	if (dynamic_allocated)
		prefix->ref_count++;
	return (prefix);
}
/**************************************************************************************************************************/
void RadixPrefixDeref(RadixPrefix *prefix)
{
	if (prefix == NULL)
		return;

	prefix->ref_count--;

	if (prefix->ref_count <= 0)
	{
		if (prefix)
			free(prefix);
		return;
	}

	return;
}
/**************************************************************************************************************************/
RadixTree *RadixTreeNew(void)
{
	RadixTree *radix;

	if ((radix = malloc(sizeof(*radix))) == NULL)
		return (NULL);
	memset(radix, '\0', sizeof(*radix));

	radix->maxbits = 128;
	radix->head = NULL;
	radix->num_active_node = 0;
	return (radix);
}
/**************************************************************************************************************************/
void RadixTreeDestroy(RadixTree *radix, RadixCBType func, void *cbctx)
{
	Clear_Radix(radix, func, cbctx);
	free(radix);
}
/**************************************************************************************************************************/
void RadixTreeProcess(RadixTree *radix, RadixCBType func, void *cbctx)
{
	RadixNode *node;

	RADIX_WALK(radix->head, node)
	{
		func(node, cbctx);
	} RADIX_WALK_END;
}
/**************************************************************************************************************************/
RadixNode *RadixTreeSearchExact(RadixTree *radix, RadixPrefix *prefix)
{
	RadixNode *node;
	unsigned char *addr;
	unsigned int bitlen;

	if (radix->head == NULL)
		return (NULL);

	node = radix->head;
	addr = RADIX_PREFIX_TO_UCHAR(prefix);
	bitlen = prefix->bitlen;

	while (node->bit < bitlen)
	{
		if (BIT_TEST(addr[node->bit >> 3], 0x80 >> (node->bit & 0x07)))
			node = node->r;
		else
			node = node->l;

		if (node == NULL)
			return (NULL);
	}

	if (node->bit > bitlen || node->prefix == NULL)
		return (NULL);

	if (comp_with_mask(RADIX_PREFIX_TO_CHAR(node->prefix), RADIX_PREFIX_TO_CHAR(prefix), bitlen))
	{
		return node;
	}

	return NULL;
}
/**************************************************************************************************************************/
RadixNode *RadixTreeSearchBest(RadixTree *radix, RadixPrefix *prefix)
{
	return (RadixTreeSearchBest2(radix, prefix, 1));
}
/**************************************************************************************************************************/
RadixNode *RadixTreeLookup(RadixTree *radix, RadixPrefix *prefix)
{
	RadixNode *node, *new_node, *parent, *glue;
	unsigned char *addr, *test_addr;
	unsigned int bitlen, check_bit, differ_bit;
	unsigned int i, j, r;

	if (radix->head == NULL)
	{
		if ((node = malloc(sizeof(*node))) == NULL)
			return (NULL);

		memset(node, '\0', sizeof(*node));

		node->bit		= prefix->bitlen;
		node->prefix	= Ref_Prefix(prefix);
		node->parent	= NULL;
		node->l			= node->r = NULL;
		node->data		= NULL;
		radix->head		= node;
		radix->num_active_node++;
		return node;
	}

	addr = RADIX_PREFIX_TO_UCHAR(prefix);
	bitlen = prefix->bitlen;
	node = radix->head;

	while (node->bit < bitlen || node->prefix == NULL)
	{
		if (node->bit < radix->maxbits && BIT_TEST(addr[node->bit >> 3], 0x80 >> (node->bit & 0x07)))
		{
			if (node->r == NULL)
				break;
			node = node->r;
		}
		else
		{
			if (node->l == NULL)
				break;
			node = node->l;
		}

		continue;
	}

	test_addr = RADIX_PREFIX_TO_UCHAR(node->prefix);

	/* find the first bit different */
	check_bit = (node->bit < bitlen) ? node->bit : bitlen;
	differ_bit = 0;

	for (i = 0; i * 8 < check_bit; i++)
	{
		if ((r = (addr[i] ^ test_addr[i])) == 0)
		{
			differ_bit = (i + 1) * 8;
			continue;
		}

		/* I know the better way, but for now */
		for (j = 0; j < 8; j++)
		{
			if (BIT_TEST(r, (0x80 >> j)))
				break;
		}

		/* must be found */
		differ_bit = i * 8 + j;
		break;
	}

	if (differ_bit > check_bit)
		differ_bit = check_bit;

	parent = node->parent;

	while (parent && parent->bit >= differ_bit)
	{
		node = parent;
		parent = node->parent;
	}

	if (differ_bit == bitlen && node->bit == bitlen)
	{
		if (node->prefix == NULL)
			node->prefix = Ref_Prefix(prefix);

		return node;
	}

	if ((new_node = malloc(sizeof(*new_node))) == NULL)
		return NULL;

	memset(new_node, '\0', sizeof(*new_node));

	new_node->bit		= prefix->bitlen;
	new_node->prefix	= Ref_Prefix(prefix);
	new_node->parent	= NULL;
	new_node->l			= new_node->r = NULL;
	new_node->data		= NULL;
	radix->num_active_node++;

	if (node->bit == differ_bit)
	{
		new_node->parent = node;

		if (node->bit < radix->maxbits && BIT_TEST(addr[node->bit >> 3], 0x80 >> (node->bit & 0x07)))
			node->r = new_node;
		else
			node->l = new_node;

		return new_node;
	}

	if (bitlen == differ_bit)
	{
		if (bitlen < radix->maxbits && BIT_TEST(test_addr[bitlen >> 3], 0x80 >> (bitlen & 0x07)))
			new_node->r = node;
		else
			new_node->l = node;

		new_node->parent = node->parent;

		if (node->parent == NULL)
			radix->head = new_node;
		else if (node->parent->r == node)
			node->parent->r = new_node;
		else
			node->parent->l = new_node;

		node->parent = new_node;
	}
	else
	{
		if ((glue = malloc(sizeof(*glue))) == NULL)
			return NULL;

		memset(glue, '\0', sizeof(*glue));

		glue->bit		= differ_bit;
		glue->prefix	= NULL;
		glue->parent	= node->parent;
		glue->data		= NULL;
		radix->num_active_node++;

		if (differ_bit < radix->maxbits && BIT_TEST(addr[differ_bit >> 3], 0x80 >> (differ_bit & 0x07)))
		{
			glue->r = new_node;
			glue->l = node;
		}
		else
		{
			glue->r = node;
			glue->l = new_node;
		}
		new_node->parent = glue;

		if (node->parent == NULL)
			radix->head = glue;
		else if (node->parent->r == node)
			node->parent->r = glue;
		else
			node->parent->l = glue;

		node->parent = glue;
	}

	return new_node;
}
/**************************************************************************************************************************/
void RadixTreeRemove(RadixTree *radix, RadixNode *node)
{
	RadixNode *parent, *child;

	if (node->r && node->l)
	{
		/* this might be a placeholder node -- have to check and make sure there is a prefix aossciated with it ! */

		if (node->prefix != NULL)
			RadixPrefixDeref(node->prefix);

		node->prefix = NULL;

		/* Also I needed to clear data pointer -- masaki */
		node->data = NULL;
		return;
	}

	if (node->r == NULL && node->l == NULL)
	{

		parent = node->parent;
		RadixPrefixDeref(node->prefix);

		if (node)
			free(node);

		radix->num_active_node--;

		if (parent == NULL)
		{
			radix->head = NULL;
			return;
		}

		if (parent->r == node)
		{
			parent->r = NULL;
			child = parent->l;
		}
		else
		{
			parent->l = NULL;
			child = parent->r;
		}

		if (parent->prefix)
			return;

		/* we need to remove parent too */
		if (parent->parent == NULL)
			radix->head = child;
		else if (parent->parent->r == parent)
			parent->parent->r = child;
		else
			parent->parent->l = child;

		child->parent = parent->parent;

		/* WARNING!! WARNING!! WARNING!! WARNING!! WARNING!! WARNING!! WARNING!! WARNING!!
		 * Evil hack: parent ptr is used below for re_stabilishing tree integrity, DONT NULLify IT HERE!!!!	 */
		if (parent)
			free (parent);

		radix->num_active_node--;
		return;
	}
	if (node->r)
		child = node->r;
	else
		child = node->l;

	parent = node->parent;
	child->parent = parent;

	RadixPrefixDeref(node->prefix);

	/* WARNING!! WARNING!! WARNING!! WARNING!! WARNING!! WARNING!! WARNING!! WARNING!!
	 * Evil hack: node ptr is used below for re_stabilishing tree integrity, DONT NULLify IT HERE!!!!	 */
	if(node)
		free(node);

	radix->num_active_node--;

	if (parent == NULL)
	{
		radix->head = child;
		return;
	}
	if (parent->r == node)
		parent->r = child;
	else
		parent->l = child;

	return;
}
/**************************************************************************************************************************/
RadixPrefix *RadixPrefixPton(const char *string, long len, const char **errmsg)
{
	char save[256], *cp, *ep;
	struct addrinfo hints, *ai;
	void *addr;
	RadixPrefix *ret;
	size_t slen;
	int r;

	ret = NULL;

	/* Copy the string to parse, because we modify it */
	if ((slen = strlen(string) + 1) > sizeof(save))
	{
		*errmsg = "string too long";
		return (NULL);
	}

	memcpy(save, string, slen);

	if ((cp = strchr(save, '/')) != NULL)
	{
		if (len != -1 )
		{
			*errmsg = "masklen specified twice";
			return (NULL);
		}

		*cp++ = '\0';
		len = strtol(cp, &ep, 10);

		if (*cp == '\0' || *ep != '\0' || len < 0)
		{
			*errmsg = "could not parse masklen";
			return (NULL);
		}
		/* More checks below */
	}
	memset(&hints, '\0', sizeof(hints));
	hints.ai_flags = AI_NUMERICHOST;

	if ((r = getaddrinfo(save, NULL, &hints, &ai)) != 0)
	{
		snprintf(save, sizeof(save), "getaddrinfo: %s:", gai_strerror(r));
		*errmsg = save;
		return NULL;
	}

	if (ai == NULL || ai->ai_addr == NULL)
	{
		*errmsg = "getaddrinfo returned no result";
		goto out;
	}

	switch (ai->ai_addr->sa_family)
	{
	case AF_INET:
		if (len == -1)
			len = 32;
		else if (len < 0 || len > 32)
			goto out;
		addr = &((struct sockaddr_in *) ai->ai_addr)->sin_addr;
		sanitise_mask(addr, len, 32);
		break;
	case AF_INET6:
		if (len == -1)
			len = 128;
		else if (len < 0 || len > 128)
			goto out;
		addr = &((struct sockaddr_in6 *) ai->ai_addr)->sin6_addr;
		sanitise_mask(addr, len, 128);
		break;
	default:
		goto out;
	}

	ret = RadixPrefixNew(ai->ai_addr->sa_family, addr, len, NULL);

	if (ret == NULL)
		*errmsg = "RadixPrefixNew failed";

	out:
	freeaddrinfo(ai);

	return ret;
}
/**************************************************************************************************************************/
RadixPrefix *RadixPrefixFromBlob(unsigned char *blob, int len, int prefixlen)
{
	int family, maxprefix;

	switch (len)
	{
	case 4:
		/* Assume AF_INET */
		family = AF_INET;
		maxprefix = 32;
		break;
	case 16:
		/* Assume AF_INET6 */
		family = AF_INET6;
		maxprefix = 128;
		break;
	default:
		/* Who knows? */
		return NULL;
	}

	if (prefixlen == -1)
		prefixlen = maxprefix;

	if (prefixlen < 0 || prefixlen > maxprefix)
		return NULL;

	return (RadixPrefixNew(family, blob, prefixlen, NULL));
}
/**************************************************************************************************************************/
const char *RadixPrefixAddrNtop(RadixPrefix *prefix, char *buf, size_t len)
{
	return (inet_ntop(prefix->family, &prefix->add, buf, len));
}
/**************************************************************************************************************************/
const char *RadixPrefixNtop(RadixPrefix *prefix, char *buf, size_t len)
{
	char addrbuf[128];

	if (RadixPrefixAddrNtop(prefix, addrbuf, sizeof(addrbuf)) == NULL)
		return NULL;

	snprintf(buf, len, "%s/%d", addrbuf, prefix->bitlen);

	return (buf);
}
/**************************************************************************************************************************/
static int comp_with_mask(unsigned char *addr, unsigned char *dest, unsigned int mask)
{
	if (memcmp(addr, dest, mask / 8) == 0) {
		unsigned int n = mask / 8;
		unsigned int m = ((~0) << (8 - (mask % 8)));

		if (mask % 8 == 0 || (addr[n] & m) == (dest[n] & m))
			return (1);
	}
	return (0);
}
/**************************************************************************************************************************/
static RadixPrefix *Ref_Prefix(RadixPrefix *prefix)
{
	if (prefix == NULL)
		return (NULL);
	if (prefix->ref_count == 0) {
		/* make a copy in case of a static prefix */
		return (RadixPrefixNew(prefix->family, &prefix->add,
				prefix->bitlen, NULL));
	}
	prefix->ref_count++;
	return (prefix);
}
/**************************************************************************************************************************/
static void Clear_Radix(RadixTree *radix, RadixCBType func, void *cbctx)
{

	if (radix->head)
	{
		RadixNode *Xstack[RADIX_MAXBITS + 1];
		RadixNode **Xsp = Xstack;
		RadixNode *Xrn = radix->head;

		while (Xrn)
		{
			RadixNode *l = Xrn->l;
			RadixNode *r = Xrn->r;

			if (Xrn->prefix)
			{
				RadixPrefixDeref(Xrn->prefix);
				if (Xrn->data && func)
					func(Xrn, cbctx);
			}

			if (Xrn)
				free(Xrn);

			radix->num_active_node--;

			if (l)
			{
				if (r)
					*Xsp++ = r;
				Xrn = l;
			}
			else if (r)
			{
				Xrn = r;
			}
			else if (Xsp != Xstack)
			{
				Xrn = *(--Xsp);
			}
			else
			{
				Xrn = (RadixNode *) 0;
			}
			continue;
		}
	}

	return;
}
/**************************************************************************************************************************/
static RadixNode *RadixTreeSearchBest2(RadixTree *radix, RadixPrefix *prefix, int inclusive)
{
	RadixNode *node;
	RadixNode *stack[RADIX_MAXBITS + 1];
	unsigned char *addr;
	unsigned int bitlen;
	int cnt = 0;

	if (radix->head == NULL)
		return NULL;

	node = radix->head;
	addr = RADIX_PREFIX_TO_UCHAR(prefix);
	bitlen = prefix->bitlen;

	while (node->bit < bitlen)
	{
		if (node->prefix)
			stack[cnt++] = node;
		if (BIT_TEST(addr[node->bit >> 3], 0x80 >> (node->bit & 0x07)))
			node = node->r;
		else
			node = node->l;

		if (node == NULL)
			break;
	}

	if (inclusive && node && node->prefix)
		stack[cnt++] = node;


	if (cnt <= 0)
		return (NULL);

	while (--cnt >= 0)
	{
		node = stack[cnt];

		if (comp_with_mask(RADIX_PREFIX_TO_CHAR(node->prefix),
				RADIX_PREFIX_TO_CHAR(prefix), node->prefix->bitlen))

			return node;
	}
	return NULL;
}
/**************************************************************************************************************************/
static void sanitise_mask(unsigned char *addr, unsigned int masklen, unsigned int maskbits)
{
	unsigned int i = masklen / 8;
	unsigned int j = masklen % 8;

	if (j != 0)
	{
		addr[i] &= (~0) << (8 - j);
		i++;
	}

	for (; i < maskbits / 8; i++)
		addr[i] = 0;

	return;
}/**************************************************************************************************************************/
/**************************************************************************************************************************/
