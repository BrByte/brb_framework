/*
 * ipv4_tableb.c
 *
 *  Created on: 2016-10-22
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2016 BrByte Software (Oliveira Alves & Amorim LTDA)
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

static RadixNode *IPv4TableRadixLookupByInAddr(IPv4Table *table, struct in_addr *addr);

/**************************************************************************************************************************/
IPv4Table *IPv4TableNew(IPv4TableConf *conf)
{
	IPv4Table *table;

	table = calloc(1, sizeof(IPv4Table));

	/* Create a new radix tree for the ipv4 table and list */
	table->tree		= RadixTreeNew();
	DLinkedListInit(&table->list, BRBDATA_THREAD_UNSAFE);

	return table;
}
/**************************************************************************************************************************/
int IPv4TableDestroy(IPv4Table *table)
{

	/* Clean up radix tree and list */
	RadixTreeDestroy(table->tree, NULL, NULL);
	DLinkedListReset(&table->list);

	/* Detach tree pointer */
	table->tree = NULL;

	/* Free shelter */
	free(table);
	return 1;
}
/**************************************************************************************************************************/
IPv4TableNode *IPv4TableItemLookupByStrAddr(IPv4Table *table, char *addr_str)
{
	struct in_addr addr;

	/* Load address and jump to binary lookup */
	addr.s_addr	= inet_addr(addr_str);
	return (IPv4TableItemLookupByInAddr(table, &addr));
}
/**************************************************************************************************************************/
IPv4TableNode *IPv4TableItemLookupByInAddr(IPv4Table *table, struct in_addr *addr)
{
	IPv4TableNode *v4_node;
	RadixNode *radix_node;

	/* Sanity check */
	if (!table)
		return NULL;

	/* Search the radix tree for this address */
	radix_node = IPv4TableRadixLookupByInAddr(table, addr);

	/* Not found */
	if (!radix_node)
		return NULL;

	/* Grab V4 node and return */
	v4_node = radix_node->data;
	return v4_node;
}
/**************************************************************************************************************************/
IPv4TableNode *IPv4TableItemAddByStrAddr(IPv4Table *table, char *addr_str)
{
	struct in_addr addr;

	/* Load address and jump to binary lookup */
	addr.s_addr	= inet_addr(addr_str);
	return (IPv4TableItemAddByInAddr(table, &addr));
}
/**************************************************************************************************************************/
IPv4TableNode *IPv4TableItemAddByInAddr(IPv4Table *table, struct in_addr *addr)
{
	IPv4TableNode *v4_node;
	RadixNode *radix_node;
	RadixPrefix prefix;

	/* Sanity check */
	if (!table)
		return NULL;

	/* Sanity check */
	if ((INADDR_NONE == addr->s_addr) || (INADDR_ANY == addr->s_addr))
		return NULL;

	/* Search the radix tree for this address */
	radix_node = IPv4TableRadixLookupByInAddr(table, addr);

	/* Found on table */
	if (radix_node)
		v4_node = radix_node->data;
	/* Not found on table, create a new RADIX NODE */
	else
	{
		/* Initialize static PREFIX with /32, as we will work with exact searches */
		RadixPrefixInit(&prefix, AF_INET, addr, 32);

		/* ALLOC new node and load ADDR */
		v4_node = calloc(1, sizeof(IPv4TableNode));
		memcpy(&v4_node->addr, addr, sizeof(struct in_addr));

		/* Update radix table */
		radix_node	= RadixTreeLookup(table->tree, &prefix);
		assert(radix_node);
		radix_node->data = v4_node;

		/* Add to list */
		DLinkedListAdd(&table->list, &v4_node->node, v4_node);
	}

	return v4_node;
}
/**************************************************************************************************************************/
int IPv4TableItemDelByStrAddr(IPv4Table *table, char *addr_str)
{
	struct in_addr addr;

	/* Load address and jump to binary lookup */
	addr.s_addr	= inet_addr(addr_str);
	return (IPv4TableItemDelByInAddr(table, &addr));
}
/**************************************************************************************************************************/
int IPv4TableItemDelByInAddr(IPv4Table *table, struct in_addr *addr)
{
	IPv4TableNode *v4_node;
	RadixNode *radix_node;

	/* Sanity check */
	if (!table)
		return 0;

	/* Search the radix tree for this address */
	radix_node = IPv4TableRadixLookupByInAddr(table, addr);

	/* Not found, leave */
	if (!radix_node)
		return 0;

	/* Grab V4 node from radix */
	v4_node = radix_node->data;

	/* Remove from list and tree */
	DLinkedListDelete(&table->list, &v4_node->node);
	RadixTreeRemove(table->tree, radix_node);

	/* Invoke destroy function, if any */
	if (v4_node->destroy_cb)
		v4_node->destroy_cb(v4_node);

	/* Free shelter */
	free(v4_node);
	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static RadixNode *IPv4TableRadixLookupByInAddr(IPv4Table *table, struct in_addr *addr)
{
	RadixPrefix prefix;
	RadixNode *radix_node;
	IPv4TableNode *v4_node;

	/* Sanity check */
	if ((INADDR_NONE == addr->s_addr) || (INADDR_ANY == addr->s_addr))
		return NULL;

	/* Initialize static PREFIX with /32, as we will work with exact searches */
	RadixPrefixInit(&prefix, AF_INET, addr, 32);

	/* Search the radix tree for this address */
	radix_node = RadixTreeSearchExact(table->tree, &prefix);
	return radix_node;
}
/**************************************************************************************************************************/
