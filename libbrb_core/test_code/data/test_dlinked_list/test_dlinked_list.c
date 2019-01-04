/*
 * test_dlinked_list.c
 *
 *  Created on: 2011-10-11
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2011 BrByte Software (Oliveira Alves & Amorim LTDA)
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

typedef struct _TestNode
{

	DLinkedListNode node;
	char name_str[64];
	int node_id;

} TestNode;

static DLinkedListCompareFunc compareNodes;

static TestNode *createNode(int id, char *name_str);
static int destroyNode(TestNode *test_node);
static int printNodes(DLinkedList *list);
static int sortNodesPrint(DLinkedList *list);

void DLinkedListSort2(DLinkedList *list, DLinkedList *ret_list, DLinkedListCompareFunc *cmp_func, DLinkedListSortCodes cmp_flag);
void DLinkedListSortSimple2(DLinkedList *list, DLinkedListCompareFunc *cmp_func, DLinkedListSortCodes cmp_flag);

/************************************************************************************************************************/
int main(void)
{
	DLinkedList test_list;
	TestNode *test_node;

	DLinkedListInit(&test_list, BRBDATA_THREAD_UNSAFE);

	printf("----------------------------------------------------------------------------------------------------\n");
	printf("----------------------------------------------------------------------------------------------------\n");
	printf("----------------------------------------------------------------------------------------------------\n");


	test_node = createNode(4, "/aaaa");
	DLinkedListAddTail(&test_list, &test_node->node, test_node);
	test_node = test_list.tail->data;
	printf("DLINKED LIST TAIL [%d]-[%s] - SZ [%lu] \n", test_node->node_id, test_node->name_str, test_list.size);

	test_node = createNode(4, "/bbbb");
	DLinkedListAddTail(&test_list, &test_node->node, test_node);
	test_node = test_list.tail->data;
	printf("DLINKED LIST TAIL [%d]-[%s] - SZ [%lu] \n", test_node->node_id, test_node->name_str, test_list.size);

	printNodes(&test_list);
	DLinkedListPopHead(&test_list);
	printNodes(&test_list);
	DLinkedListPopHead(&test_list);
	printNodes(&test_list);

	test_node = createNode(8, "/cccc.txt");
	DLinkedListAddTail(&test_list, &test_node->node, test_node);
	test_node = test_list.tail->data;
	printf("DLINKED LIST TAIL [%d]-[%s] - SZ [%lu] \n", test_node->node_id, test_node->name_str, test_list.size);

	test_node = createNode(8, "/eeee.txt");
	DLinkedListAddTail(&test_list, &test_node->node, test_node);
	test_node = test_list.tail->data;
	printf("DLINKED LIST TAIL [%d]-[%s] - SZ [%lu] \n", test_node->node_id, test_node->name_str, test_list.size);

	printNodes(&test_list);
	DLinkedListPopHead(&test_list);
	printNodes(&test_list);

	test_node = createNode(8, "/dddd.txt");
	DLinkedListAddTail(&test_list, &test_node->node, test_node);
	test_node = test_list.tail->data;
	printf("DLINKED LIST TAIL [%d]-[%s] - SZ [%lu] \n", test_node->node_id, test_node->name_str, test_list.size);

	printNodes(&test_list);
	DLinkedListPopHead(&test_list);
	printNodes(&test_list);

	test_node = createNode(4, "/abcd");
	DLinkedListAddTail(&test_list, &test_node->node, test_node);
	test_node = test_list.tail->data;
	printf("DLINKED LIST TAIL [%d]-[%s] - SZ [%lu] \n", test_node->node_id, test_node->name_str, test_list.size);

	printNodes(&test_list);

//	test_node = createNode(130);
//	DLinkedListAddTail(&test_list, &test_node->node, test_node);
//	test_node = test_list.tail->data;
//	printf("DLINKED LIST TAIL [%d] - SZ [%lu] \n", test_node->node_id, test_list.size);
//
//	test_node = createNode(121);
//	DLinkedListAddTail(&test_list, &test_node->node, test_node);
//	test_node = test_list.tail->data;
//	printf("DLINKED LIST TAIL [%d] - SZ [%lu] \n", test_node->node_id, test_list.size);
//
//	test_node = createNode(128);
//	DLinkedListAddTail(&test_list, &test_node->node, test_node);
//	test_node = test_list.tail->data;
//	printf("DLINKED LIST TAIL [%d] - SZ [%lu] \n", test_node->node_id, test_list.size);

	printf(" P 01 ----------------------------------------------------------------------------------------------\n");
	printNodes(&test_list);
	printf(" SORT ----------------------------------------------------------------------------------------------\n");
	DLinkedListSortSimple(&test_list, compareNodes, 1);
	printf(" P 02 ----------------------------------------------------------------------------------------------\n");
	printNodes(&test_list);
	printf(" SORT ----------------------------------------------------------------------------------------------\n");
	DLinkedListSortSimple(&test_list, compareNodes, 0);
	printf(" P 03 ----------------------------------------------------------------------------------------------\n");
	printNodes(&test_list);
	printf("----------------------------------------------------------------------------------------------------\n");
//	sortNodesPrint(&test_list);
//	printf("----------------------------------------------------------------------------------------------------\n");
//	sortNodesPrint(&test_list);
//	printf("----------------------------------------------------------------------------------------------------\n");
//	sortNodesPrint(&test_list);
//	printf("----------------------------------------------------------------------------------------------------\n");
//	printf("----------------------------------------------------------------------------------------------------\n");

//	test_node = test_list.head->data;
//	printf("DLINKED LIST HEAD [%d] - SZ [%lu] \n", test_node->node_id, test_list.size);
//	test_node = test_list.tail->data;
//	printf("DLINKED LIST TAIL [%d] - SZ [%lu] \n", test_node->node_id, test_list.size);
//	test_node = DLinkedListPopHead(&test_list);
//	destroyNode(test_node);
//
//	test_node = test_list.head->data;
//	printf("DLINKED LIST HEAD [%d] - SZ [%lu] \n", test_node->node_id, test_list.size);
//	test_node = test_list.tail->data;
//	printf("DLINKED LIST TAIL [%d] - SZ [%lu] \n", test_node->node_id, test_list.size);
//	test_node = DLinkedListPopHead(&test_list);
//	destroyNode(test_node);
//
//	test_node = test_list.head->data;
//	printf("DLINKED LIST HEAD [%d] - SZ [%lu] \n", test_node->node_id, test_list.size);
//	test_node = test_list.tail->data;
//	printf("DLINKED LIST TAIL [%d] - SZ [%lu] \n", test_node->node_id, test_list.size);
//	test_node = DLinkedListPopHead(&test_list);
//	destroyNode(test_node);
//
//	printf("----------------------------------------------------------------------------------------------------\n");
//	printf("----------------------------------------------------------------------------------------------------\n");
//	printf("----------------------------------------------------------------------------------------------------\n");


}
/************************************************************************************************************************/
static int sortNodesPrint(DLinkedList *list)
{
	DLinkedList list_ordered;

	memset(&list_ordered, 0, sizeof(DLinkedList));

//	DLinkedListDup(&list, &list_ordered)
//	DLinkedListSort(list, &list_ordered, compareNodes, 1);
	DLinkedListSort2(list, &list_ordered, compareNodes, 1);

	printf(" SORTED -------------------------------------------------------------\n");
	printNodes(&list_ordered);

	DLinkedListClean(&list_ordered);

	return 1;
}
/************************************************************************************************************************/
static TestNode *createNode(int id, char *name_str)
{
	TestNode *test_node;

	test_node = calloc(1, sizeof(TestNode));

	test_node->node_id = id;
	strlcpy((char *)&test_node->name_str, name_str, sizeof(test_node->name_str));

	return test_node;
}
/************************************************************************************************************************/
static int destroyNode(TestNode *test_node)
{
	if (!test_node)
		return 0;

	/* free */
	free(test_node);

	return 1;
}
/************************************************************************************************************************/
static int printNodes(DLinkedList *list)
{
	TestNode *test_node;
	TestNode *test_node_p;
	TestNode *test_node_n;
	TestNode *test_node_px;
	TestNode *test_node_nx;
	DLinkedListNode *node;
	int i;

	/* sanitize and check if list have items */
	if (!list)
		return 0;

	printf("DLINKED PRINT NODES [%lu] - EMPTY [%d] ------------------------------------\n", list->size, DLINKED_LIST_PTR_ISEMPTY(list));

	for (i = 0, node = list->head; node; node = node->next, i++)
	{
		/* grab data info */
		test_node 		= node->data;

		/* check if destination is valid */
		if (!test_node)
			continue;

		test_node_p		= node->prev ? node->prev->data : NULL;
		test_node_n		= node->next ? node->next->data : NULL;

		test_node_px	= test_node->node.prev ? test_node->node.prev->data : NULL;
		test_node_nx	= test_node->node.next ? test_node->node.next->data : NULL;

//		printf("DLINKED NODE [%d] of [%lu] - VALUE [%d][%s] - PREV [%d][%d] - NEXT [%d][%d]\n", i, list->size,
//				test_node->node_id, test_node->name_str,
//				test_node_p ? test_node_p->node_id : -1,
//				test_node_p ? test_node_p->node_id : -1,
//				test_node_n ? test_node_n->node_id : -1,
//				test_node_n ? test_node_n->node_id : -1);


		printf("DLINKED NODE [%d] of [%lu] - VALUE [%d][%s] - PREV [%s][%s] - NEXT [%s][%s]\n", i, list->size,
				test_node->node_id, test_node->name_str,
				test_node_p ? test_node_p->name_str : "-",
				test_node_p ? test_node_p->name_str : "-",
				test_node_n ? test_node_n->name_str : "-",
				test_node_n ? test_node_n->name_str : "-");

		continue;
	}


	return 1;
}
/************************************************************************************************************************/
static int compareNodes(DLinkedListNode *node, DLinkedListNode *node_cmp)
{
	TestNode *test_node		= node->data;
	TestNode *test_node_cmp	= node_cmp->data;
	int op_status;

	if (test_node->node_id < test_node_cmp->node_id)
		return 0;

//	op_status  		= WebEngineBaseSortByString((char *)&test_node->name_str, (char *)&test_node_cmp->name_str);

	int str_sz;
	int str_szcmp;
	int i;
	int limit;
	char *str		= (char *)&test_node->name_str;
	char *strcmp	= (char *)&test_node_cmp->name_str;

	/* Sanity check */
	if ((!str) || (!strcmp))
		return 0;

	str_sz		= strlen(str);
	str_szcmp	= strlen(strcmp);

	/* Nothing to compare, empty field */
	if (0 == str_sz)
		return 1;

	limit = ( str_szcmp > str_sz ? str_sz : str_szcmp);

	for (i = 0; i < limit; i++)
	{
		/* Char is the same, avoid it */
		if (str[i] == strcmp[i])
			continue;

		/* Found different char, avail and return from here */
		if (str[i] > strcmp[i])
			return 1;
		else
			return 0;

	}

	/* strings begin with the same characters, check who have more characters to ordenate  */
	return ( str_szcmp > str_sz ? 0 : 1);
}
/**************************************************************************************************************************/
/************************************************************************************************************************/
void DLinkedListSort2(DLinkedList *list, DLinkedList *ret_list, DLinkedListCompareFunc *cmp_func, DLinkedListSortCodes cmp_flag)
{

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_LOCK(list->mutex, "DLINKED_LIST");

	/* Return empty list */
	if (!list->head)
	{
		/* Running THREAD_SAFE, UNLOCK MUTEX */
		if (list->flags.thread_safe)
			MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

		return;
	}

	/* Duplicate into return list and calculate limit */
	DLinkedListDup(list, ret_list);

	DLinkedListSortSimple2(ret_list, cmp_func, cmp_flag);

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

	return;
}
/************************************************************************************************************************/
static int ShowNodes(DLinkedListNode *node_a, DLinkedListNode *node_b)
{
	TestNode *test_node_a		= node_a->data;
	TestNode *test_node_b		= node_b->data;

	TestNode *test_node_a_p 	= node_a->prev ? node_a->prev->data : NULL;
	TestNode *test_node_a_n 	= node_a->next ? node_a->next->data : NULL;
	TestNode *test_node_b_p 	= node_b->prev ? node_b->prev->data : NULL;
	TestNode *test_node_b_n 	= node_b->next ? node_b->next->data : NULL;

	TestNode *test_node_a_px 	= test_node_a->node.prev ? test_node_a->node.prev->data : NULL;
	TestNode *test_node_a_nx 	= test_node_a->node.next ? test_node_a->node.next->data : NULL;
	TestNode *test_node_b_px 	= test_node_b->node.prev ? test_node_b->node.prev->data : NULL;
	TestNode *test_node_b_nx 	= test_node_b->node.next ? test_node_b->node.next->data : NULL;

//	printf("NODE A [%d] [%d]>[%d] [%d]>[%d] - NODE B [%d] [%d]>[%d] [%d]>[%d]\n",
//			test_node_a->node_id, test_node_a_p ? test_node_a_p->node_id : -1, test_node_a_n ? test_node_a_n->node_id : -1,
//			test_node_a_px ? test_node_a_px->node_id : -1, test_node_a_nx ? test_node_a_nx->node_id : -1,
//			test_node_b->node_id, test_node_b_p ? test_node_b_p->node_id : -1, test_node_b_n ? test_node_b_n->node_id : -1,
//			test_node_b_nx ? test_node_b_nx->node_id : -1, test_node_b_nx ? test_node_b_nx->node_id : -1);

	printf("NODE A [%s] [%s]>[%s] [%s]>[%s] - NODE B [%s] [%s]>[%s] [%s]>[%s]\n",
			test_node_a->name_str, test_node_a_p ? test_node_a_p->name_str : "-", test_node_a_n ? test_node_a_n->name_str : "-",
			test_node_a_px ? test_node_a_px->name_str : "-", test_node_a_nx ? test_node_a_nx->name_str : "-",
			test_node_b->name_str, test_node_b_p ? test_node_b_p->name_str : "-", test_node_b_n ? test_node_b_n->name_str : "-",
			test_node_b_nx ? test_node_b_nx->name_str : "-", test_node_b_nx ? test_node_b_nx->name_str : "-");

	return 0;
}
/**************************************************************************************************************************/
void DLinkedListSortSimple2(DLinkedList *list, DLinkedListCompareFunc *cmp_func, DLinkedListSortCodes cmp_flag)
{
	DLinkedListNode *node;
	DLinkedListNode *tmp_node_ptr;
	void *tmp_data_ptr;
	int limit;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_LOCK(list->mutex, "DLINKED_LIST");

	/* Return empty list */
	if (!list->head)
	{
		/* Running THREAD_SAFE, UNLOCK MUTEX */
		if (list->flags.thread_safe)
			MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

		return;
	}

	limit = list->size + 1;

	/* Do the sorting */
	for (node = list->head; node && (limit > 0); node = node->next, limit--)
	{
		for (tmp_node_ptr = node->next; tmp_node_ptr; tmp_node_ptr = tmp_node_ptr->next)
		{
			/* Invoke the compare function to check if it is lower or greater */
			if (cmp_flag ? cmp_func(node, tmp_node_ptr) : !cmp_func(node, tmp_node_ptr))
			{
				ShowNodes(node, tmp_node_ptr);

				tmp_data_ptr		= node->data;
				node->data			= tmp_node_ptr->data;
//				tmp_node_ptr->prev 	= node->prev;
				tmp_node_ptr->data	= tmp_data_ptr;
			}

			continue;
		}

		continue;
	}

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

	return;
}
/************************************************************************************************************************/
