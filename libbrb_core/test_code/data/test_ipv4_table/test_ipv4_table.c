/*
 * test_ipv4_table.c
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

#include <libbrb_core.h>

/************************************************************************************************************************/
int main(int argc, char **argv)
{
	IPv4Table *v4_table;
	IPv4TableNode *v4_node;
	IPv4TableConf conf;

	/* Clean up stack */
	memset(&conf, 0, sizeof(IPv4TableConf));

	v4_table = IPv4TableNew(&conf);

	v4_node = IPv4TableItemLookupByStrAddr(v4_table, "5.5.5.5");

	if (!v4_node)
	{
		printf("Node not found\n");
	}

	v4_node = IPv4TableItemAddByStrAddr(v4_table, "5.5.5.5");
	printf("Added 5.5.5.5 node at [%p]\n", v4_node);
	v4_node = IPv4TableItemAddByStrAddr(v4_table, "6.5.5.5");
	printf("Added 6.5.5.5 node at [%p]\n", v4_node);
	v4_node = IPv4TableItemAddByStrAddr(v4_table, "7.5.5.5");
	printf("Added 7.5.5.5 node at [%p]\n", v4_node);

	printf("--------------------------\n");
	v4_node = IPv4TableItemLookupByStrAddr(v4_table, "5.5.5.5");
	printf("Found 5.5.5.5 node node at [%p]\n", v4_node);
	v4_node = IPv4TableItemLookupByStrAddr(v4_table, "6.5.5.5");
	printf("Found 6.5.5.5 node node at [%p]\n", v4_node);
	v4_node = IPv4TableItemLookupByStrAddr(v4_table, "7.5.5.5");
	printf("Found 7.5.5.5 node node at [%p]\n", v4_node);

	printf("--------------------------\n");
	IPv4TableItemDelByStrAddr(v4_table, "5.5.5.5");
	printf("Delete 5.5.5.5\n");
	IPv4TableItemDelByStrAddr(v4_table, "7.5.5.5");
	printf("Delete 7.5.5.5\n");

	printf("--------------------------\n");
	v4_node = IPv4TableItemLookupByStrAddr(v4_table, "5.5.5.5");
	printf("Found 5.5.5.5 node node at [%p]\n", v4_node);
	v4_node = IPv4TableItemLookupByStrAddr(v4_table, "6.5.5.5");
	printf("Found 6.5.5.5 node node at [%p]\n", v4_node);
	v4_node = IPv4TableItemLookupByStrAddr(v4_table, "7.5.5.5");
	printf("Found 7.5.5.5 node node at [%p]\n", v4_node);

	IPv4TableDestroy(v4_table);
	return 0;
}
/************************************************************************************************************************/
