/*
 * dlinked_list.c
 *
 *  Created on: 2013-02-04
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2013 BrByte Software (Oliveira Alves & Amorim LTDA)
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

static void DLinkedListAddUnsafe(DLinkedList *list, DLinkedListNode *node, void *data);
static void DLinkedListDupUnsafe(DLinkedList *list, DLinkedList *ret_list);
static void DLinkedListDupFilterUnsafe(DLinkedList *list, DLinkedList *ret_list, DLinkedListFilterNode *filter_func, char *filter_key, char *filter_value);
static void DLinkedListSortMergeUnsafe(DLinkedList *list, DLinkedListCompareFunc *cmp_func, DLinkedListSortCodes cmp_flag);

static DLinkedListNode *DLinkedListMergeSortFunc(DLinkedListNode *head, DLinkedListCompareFunc *cmp_func, DLinkedListSortCodes cmp_flag);

/**************************************************************************************************************************/
void DLinkedListInit(DLinkedList *list, LibDataThreadSafeType type)
{
	/* Clean up list data */
	memset(list, 0, sizeof(DLinkedList));

	/* Set MT_FLAGS */
	list->flags.thread_safe	= ((BRBDATA_THREAD_SAFE == type) ? 1 : 0);
	list->mutex				= (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;

	return;
}
/**************************************************************************************************************************/
void DLinkedListReset(DLinkedList *list)
{
	/* Sanity check */
	if (!list)
		return;

	/* Running thread safe, destroy MUTEX */
	if (list->flags.thread_safe)
		MUTEX_DESTROY(list->mutex, "DLINKED_LIST");

	/* Reset LIST */
	list->mutex	= (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	list->head = NULL;
	list->tail = NULL;
	list->size = 0;

	return;
}
/**************************************************************************************************************************/
void DLinkedListAddDebug(DLinkedList *list, DLinkedListNode *node, void *data)
{
	DLinkedListNode *aux_node;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_LOCK(list->mutex, "DLINKED_LIST");

	/* Walk the list to see if this node address is known */
	for (aux_node = list->head; aux_node; aux_node = aux_node->next)
		assert(aux_node != node);

	node->data = data;
	node->prev = NULL;
	node->next = list->head;

	if (list->head)
		list->head->prev = node;

	list->head = node;

	if (list->tail == NULL)
		list->tail = node;

	list->size++;

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

	return;
}
/**************************************************************************************************************************/
void DLinkedListAddHead(DLinkedList *list, DLinkedListNode *node, void *data)
{
	return DLinkedListAdd(list, node, data);
}
/**************************************************************************************************************************/
void DLinkedListAdd(DLinkedList *list, DLinkedListNode *node, void *data)
{
	/* Running THREAD_SAFE, LOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_LOCK(list->mutex, "DLINKED_LIST");

	node->data = data;
	node->prev = NULL;
	node->next = list->head;

	if (list->head)
		list->head->prev = node;

	list->head = node;

	if (list->tail == NULL)
		list->tail = node;

	list->size++;

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

	return;
}
/**************************************************************************************************************************/
void DLinkedListAddTailDebug(DLinkedList *list, DLinkedListNode *node, void *data)
{
	DLinkedListNode *aux_node;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_LOCK(list->mutex, "DLINKED_LIST");

	/* Walk the list to see if this node address is known */
	for (aux_node = list->head; aux_node; aux_node = aux_node->next)
		assert(aux_node != node);

	node->data = data;
	node->next = NULL;
	node->prev = list->tail;

	if (list->tail)
		list->tail->next = node;

	list->tail = node;

	if (list->head == NULL)
		list->head = node;

	list->size++;

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

	return;
}
/**************************************************************************************************************************/
void DLinkedListAddTail(DLinkedList *list, DLinkedListNode *node, void *data)
{
	/* Running THREAD_SAFE, LOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_LOCK(list->mutex, "DLINKED_LIST");

	node->data = data;
	node->next = NULL;
	node->prev = list->tail;

	if (list->tail)
		list->tail->next = node;

	list->tail = node;

	if (list->head == NULL)
		list->head = node;

	list->size++;

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

	return;
}
/**************************************************************************************************************************/
void DLinkedListAddBefore(DLinkedList *list, DLinkedListNode *node_pos, DLinkedListNode *node_new, void *data)
{
	/* sanitize */
	if (!list || !node_pos || !node_new || !data)
		return;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_LOCK(list->mutex, "DLINKED_LIST");

	node_new->data 		= data;
	node_new->prev 		= node_pos->prev;
	node_new->next 		= node_pos;
	node_pos->prev 		= node_new;

	if (node_new->prev)
		node_new->prev->next 	= node_new;

	if (list->head == NULL)
		list->head 	= node_new;
	else if (list->head == node_pos)
		list->head 	= node_new;

	if (list->tail == NULL)
		list->tail = node_new;

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

	return;
}
/**************************************************************************************************************************/
void DLinkedListAddAfter(DLinkedList *list, DLinkedListNode *node_pos, DLinkedListNode *node_new, void *data)
{
	/* sanitize */
	if (!list || !node_pos || !node_new || !data)
		return;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_LOCK(list->mutex, "DLINKED_LIST");

	node_new->data 		= data;
	node_new->prev 		= node_pos;
	node_new->next 		= node_pos->next;

	node_pos->next 		= node_new;

	if (node_new->next)
		node_new->next->prev 	= node_new;

	if (list->head == NULL)
		list->head 	= node_new;

	if (list->tail == NULL)
		list->tail = node_new;
	else if (list->tail == node_pos)
		list->tail 	= node_new;

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

	return;
}
/**************************************************************************************************************************/
void DLinkedListAddPos(DLinkedList *list, unsigned long pos, DLinkedListNode *node, void *data)
{
	DLinkedListNode *node_ptr;
	unsigned int i;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_LOCK(list->mutex, "DLINKED_LIST");

	/* Initialize data */
	node_ptr	= list->head;
	i 			= 0;

	if (pos <= 0)
		goto add_head;

	/* Position exceeds list size - Just add on tail */
	if (pos >= list->size)
		goto add_tail;

	while (1)
	{
		/* THIS SHOULD NOT HAPPEN - Finished list, just add to tail */
		if (!node_ptr)
		{
			goto add_tail;
			break; /* NEVER REACHED */
		}

		/* Found correct position inside list */
		if (pos == i)
		{
			if (node_ptr->prev)
				node_ptr->prev->next 	= node;

			node->next 		= node_ptr;
			node->prev 		= node_ptr->prev;
			node_ptr->prev 	= node;

			break;
		}

		node_ptr	= node_ptr->next;
		i++;
		continue;
	}

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

	return;

	/* Add to tail without touching the MUTEX */
	add_tail:
	node->data = data;
	node->next = NULL;
	node->prev = list->tail;

	if (list->tail)
		list->tail->next = node;

	list->tail = node;

	if (list->head == NULL)
		list->head = node;

	list->size++;

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");
	return;

	/* Common add without touching MUTEX */
	add_head:
	node->data = data;
	node->prev = NULL;
	node->next = list->head;

	if (list->head)
		list->head->prev = node;

	list->head = node;

	if (list->tail == NULL)
		list->tail = node;

	list->size++;

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");
	return;
}
/**************************************************************************************************************************/
void DLinkedListMoveToTail(DLinkedList *list, DLinkedListNode *node)
{
	/* Running THREAD_SAFE, LOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_LOCK(list->mutex, "DLINKED_LIST");

	/* Do nothing */
	if (node == list->tail)
	{
		/* Running THREAD_SAFE, UNLOCK MUTEX */
		if (list->flags.thread_safe)
			MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

		return;
	}

	/* This node is orphan, direct add */
	if ((list->head != node) && (NULL == node->next) && (NULL == node->prev))
		goto add_to_tail;

	/* reorganize reference from node in next and previous */
	if (node->next)
		node->next->prev = node->prev;

	if (node->prev)
		node->prev->next = node->next;

	if (node == list->head)
		list->head = node->next;

	if (node == list->tail)
		list->tail = node->prev;

	add_to_tail:

	/* Add to tail */
	node->next = NULL;
	node->prev = list->tail;

	if (list->tail)
		list->tail->next = node;

	list->tail = node;

	if (list->head == NULL)
		list->head = node;

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

	return;
}
/**************************************************************************************************************************/
void DLinkedListMoveToHead(DLinkedList *list, DLinkedListNode *node)
{
	/* Running THREAD_SAFE, LOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_LOCK(list->mutex, "DLINKED_LIST");

	/* Do nothing */
	if (node == list->head)
	{
		/* Running THREAD_SAFE, UNLOCK MUTEX */
		if (list->flags.thread_safe)
			MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

		return;
	}

	/* This node is orphan, direct add */
	if ((list->head != node) && (NULL == node->next) && (NULL == node->prev))
		goto add_to_head;

	/* reorganize reference from node in next and previous */
	if (node->next)
		node->next->prev = node->prev;

	if (node->prev)
		node->prev->next = node->next;

	if (node == list->head)
		list->head = node->next;

	if (node == list->tail)
		list->tail = node->prev;

	add_to_head:

	/* Add to head */
	node->prev = NULL;
	node->next = list->head;

	if (list->head)
		list->head->prev = node;

	list->head = node;

	if (list->tail == NULL)
		list->tail = node;

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

	return;
}
/**************************************************************************************************************************/
void DLinkedListDeleteDebug(DLinkedList *list, DLinkedListNode *node)
{
	DLinkedListNode *aux_node;
	int node_found;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_LOCK(list->mutex, "DLINKED_LIST");

	/* Walk the list to see if this node address is known */
	for (node_found = 0, aux_node = list->head; aux_node; aux_node = aux_node->next)
	{
		if (aux_node == node)
		{
			node_found = 1;
			break;
		}
		continue;
	}

	assert(node_found);

	/* This node is orphan */
	if ((list->head != node) && (NULL == node->next) && (NULL == node->prev))
	{
		/* Running THREAD_SAFE, UNLOCK MUTEX */
		if (list->flags.thread_safe)
			MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");
		return;
	}

	if (node->next)
		node->next->prev = node->prev;

	if (node->prev)
		node->prev->next = node->next;

	if (node == list->head)
		list->head = node->next;

	if (node == list->tail)
		list->tail = node->prev;

	list->size--;

	node->next = node->prev = NULL;

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

	return;
}
/**************************************************************************************************************************/
void DLinkedListDelete(DLinkedList *list, DLinkedListNode *node)
{
	/* Running THREAD_SAFE, LOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_LOCK(list->mutex, "DLINKED_LIST");

	/* This node is orphan */
	if ((list->head != node) && (NULL == node->next) && (NULL == node->prev))
	{
		/* Running THREAD_SAFE, UNLOCK MUTEX */
		if (list->flags.thread_safe)
			MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

		return;
	}

	if (node->next)
		node->next->prev = node->prev;

	if (node->prev)
		node->prev->next = node->next;

	if (node == list->head)
		list->head = node->next;

	if (node == list->tail)
		list->tail = node->prev;

	list->size--;

	/* Orphanize NODE */
	node->next = NULL;
	node->prev = NULL;

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

	return;
}
/**************************************************************************************************************************/
void *DLinkedListPopTail(DLinkedList *list)
{
	DLinkedListNode *node;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_LOCK(list->mutex, "DLINKED_LIST");

	if (list->tail == NULL)
	{
		/* Running THREAD_SAFE, UNLOCK MUTEX */
		if (list->flags.thread_safe)
			MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

		return NULL;
	}

	node = list->tail;

	/* Head and tail are the same, only item on list */
	if (node == list->head)
	{
		list->head = NULL;
		list->tail = NULL;
	}

	/* Unlink from previous node */
	if (node->prev)
		node->prev->next = NULL;

	/* Set new tail to previous node */
	list->tail = node->prev;

	/* Unlink local node */
	node->next = NULL;
	node->prev = NULL;

	list->size--;

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

	return node->data;
}
/**************************************************************************************************************************/
void *DLinkedListPopHead(DLinkedList *list)
{
	DLinkedListNode *node;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_LOCK(list->mutex, "DLINKED_LIST");

	if (list->head == NULL)
	{
		/* Running THREAD_SAFE, UNLOCK MUTEX */
		if (list->flags.thread_safe)
			MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

		return NULL;
	}

	node = list->head;

	if (node->next)
		node->next->prev = node->prev;

	if (node->prev)
		node->prev->next = node->next;

	if (node == list->head)
		list->head = node->next;

	if (node == list->tail)
		list->tail = node->prev;

	node->next = node->prev = NULL;

	list->size--;

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

	return node->data;
}
/**************************************************************************************************************************/
void DLinkedListDestroyData(DLinkedList *list)
{
	DLinkedListNode *node;
	DLinkedListNode *prev_node;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_LOCK(list->mutex, "DLINKED_LIST");

	node = list->head;

	while (node)
	{
		/* Walking */
		prev_node	= node;
		node		= node->next;

		DLinkedListDelete(list, prev_node);

		/* Destroy the data and node */
		free(prev_node->data);
		free(prev_node);

		continue;
	}

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

	return;
}
/**************************************************************************************************************************/
void DLinkedListDup(DLinkedList *list, DLinkedList *ret_list)
{
	DLinkedListNode *node;
	DLinkedListNode *new_node;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_LOCK(list->mutex, "DLINKED_LIST");

	for (node = list->head; node; node = node->next)
	{
		new_node = calloc(1, sizeof(DLinkedListNode));
		new_node->data = node->data;
		DLinkedListAdd(ret_list, new_node, new_node->data);

		continue;
	}

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

	return;
}
/**************************************************************************************************************************/
void DLinkedListDupFilter(DLinkedList *list, DLinkedList *ret_list, DLinkedListFilterNode *filter_func, char *filter_key, char *filter_value)
{
	DLinkedListNode *node;
	DLinkedListNode *new_node;
	int filter_node;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_LOCK(list->mutex, "DLINKED_LIST");

	for (node = list->head; node; node = node->next)
	{
		if (!filter_func)
			continue;

		filter_node = filter_func(node, filter_key, filter_value);

		/* filter node */
		if (filter_node)
			continue;

		/* copy node */
		new_node = calloc(1, sizeof(DLinkedListNode));
		new_node->data = node->data;
		DLinkedListAdd(ret_list, new_node, new_node->data);

	}

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

	return;
}
/**************************************************************************************************************************/
void DLinkedListClean(DLinkedList *list)
{
	DLinkedListNode *node;
	DLinkedListNode *prev_node;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_LOCK(list->mutex, "DLINKED_LIST");

	for (node = list->head; node; node = node->next)
	{
		loop_without_move:

		/* Sanity check */
		if (!node)
			break;

		/* Save reference pointers */
		prev_node = node;
		node = node->next;

		/* Remove from pending request list */
		DLinkedListDelete(list, prev_node);

		/* Destroy the node */
		free(prev_node);

		goto loop_without_move;
	}

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

	return;
}
/**************************************************************************************************************************/
void DLinkedListSort(DLinkedList *list, DLinkedList *ret_list, DLinkedListCompareFunc *cmp_func, DLinkedListSortCodes cmp_flag)
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
	DLinkedListDupUnsafe(list, ret_list);

	/* Sort Duplicated List */
	DLinkedListSortMergeUnsafe(ret_list, cmp_func, cmp_flag);

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

	return;
}
/**************************************************************************************************************************/
void DLinkedListSortFilter(DLinkedList *list, DLinkedList *ret_list, DLinkedListCompareFunc *cmp_func, DLinkedListSortCodes cmp_flag,
		DLinkedListFilterNode *filter_func, char *filter_key, char *filter_value)
{
	DLinkedListNode *node;
	DLinkedListNode *new_node;
	int filter_node;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_LOCK(list->mutex, "DLINKED_LIST");

	/* Return empty list */
	if (!list->head)
	{
		/* Initialize empty list */
		if (!ret_list->head)
			DLinkedListInit(ret_list, (list->flags.thread_safe ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE));

		/* Running THREAD_SAFE, UNLOCK MUTEX */
		if (list->flags.thread_safe)
			MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

		return;
	}

	/* Duplicate into return list and calculate limit */
	DLinkedListDupFilterUnsafe(list, ret_list, filter_func, filter_key, filter_value);

	/* Sort Duplicated List */
	DLinkedListSortMergeUnsafe(ret_list, cmp_func, cmp_flag);

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

	return;
}
/**************************************************************************************************************************/
void DLinkedListBubbleSort(DLinkedList *list, DLinkedList *ret_list, DLinkedListCompareFunc *cmp_func, DLinkedListSortCodes cmp_flag)
{
	/* Running THREAD_SAFE, LOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_LOCK(list->mutex, "DLINKED_LIST");

	/* Return empty list */
	if (!list->head)
	{
		/* Initialize empty list */
		DLinkedListInit(ret_list, (list->flags.thread_safe ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE));

		/* Running THREAD_SAFE, UNLOCK MUTEX */
		if (list->flags.thread_safe)
			MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

		return;
	}

	/* Duplicate LIST */
	DLinkedListDup(list, ret_list);

	/* Sort Duplicated List */
	DLinkedListSortBubble(ret_list, cmp_func, cmp_flag);

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

	return;
}
/**************************************************************************************************************************/
void DLinkedListMergeSort(DLinkedList *list, DLinkedList *ret_list, DLinkedListCompareFunc *cmp_func, DLinkedListSortCodes cmp_flag)
{
	/* Running THREAD_SAFE, LOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_LOCK(list->mutex, "DLINKED_LIST");

	/* Return empty list */
	if (!list->head)
	{
		/* Initialize empty list */
		DLinkedListInit(ret_list, (list->flags.thread_safe ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE));

		/* Running THREAD_SAFE, UNLOCK MUTEX */
		if (list->flags.thread_safe)
			MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

		return;
	}

	/* duplicate the list to return the list ordered without alter original list */
	DLinkedListDup(list, ret_list);

	/* Sort Duplicated List */
	DLinkedListSortMerge(ret_list, cmp_func, cmp_flag);

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

	return;
}
/**************************************************************************************************************************/
/* SORT FUNCTIONS */
/**************************************************************************************************************************/
void DLinkedListSortSimple(DLinkedList *list, DLinkedListCompareFunc *cmp_func, DLinkedListSortCodes cmp_flag)
{
	DLinkedListSortMerge(list, cmp_func, cmp_flag);

	return;
}
/**************************************************************************************************************************/
void DLinkedListSortBubble(DLinkedList *list, DLinkedListCompareFunc *cmp_func, DLinkedListSortCodes cmp_flag)
{
	DLinkedListSortMerge(list, cmp_func, cmp_flag);

	return;
}
/**************************************************************************************************************************/
void DLinkedListSortMerge(DLinkedList *list, DLinkedListCompareFunc *cmp_func, DLinkedListSortCodes cmp_flag)
{
	DLinkedListNode *node;

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

	/* check if the return list has more than one item */
	if (list->size > 1)
	{
		/* call the function to order */
		node 			= list->head;
		list->head 		= DLinkedListMergeSortFunc(node, cmp_func, cmp_flag);

		node			= list->head;

		while(node->next)
			node		= node->next;

		list->tail 		= node;
	}

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (list->flags.thread_safe)
		MUTEX_UNLOCK(list->mutex, "DLINKED_LIST");

	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static DLinkedListNode *DLinkedListMergeSortFunc(DLinkedListNode *head, DLinkedListCompareFunc *cmp_func, DLinkedListSortCodes cmp_flag)
{
	/* Private function with no LOCKING */

	/* Trivial case: length 0 or 1 */
	if (!head || !head->next)
		return head;

	DLinkedListNode *right	= head;
	DLinkedListNode *temp	= head;
	DLinkedListNode *last	= head;
	DLinkedListNode *result = 0;
	DLinkedListNode *next	= 0;
	DLinkedListNode *tail	= 0;

	/* Find halfway through the list (by running two pointers, one at twice the speed of the other) */
	while (temp && temp->next)
	{
		last	= right;
		right	= right->next;
		temp	= temp->next->next;

		continue;
	}

	/* Break the list in two - node->prev are broken here, but we fix it later */
	last->next = 0;

	/* Recurse on the two smaller lists */
	head	= DLinkedListMergeSortFunc(head, cmp_func, cmp_flag);
	right	= DLinkedListMergeSortFunc(right, cmp_func, cmp_flag);

	/* Merge lists */
	while (head || right)
	{
		/* Take from empty right list */
		if (!right)
		{
			next = head;
			head = head->next;
		}
		/* Take from empty head list */
		else if (!head)
		{
			next = right;
			right = right->next;
		}
		/* compare elements to ordering */
		else if (cmp_flag ? !cmp_func(head, right) : cmp_func(head, right))
		{
			next = head;
			head = head->next;
		}
		else
		{
			next = right;
			right = right->next;
		}

		if (!result)
			result = next;
		else
			tail->next = next;

		/* fixed previous pointer */
		next->prev	= tail;
		tail		= next;

		continue;
	}

	return result;

}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void DLinkedListAddUnsafe(DLinkedList *list, DLinkedListNode *node, void *data)
{
	node->data = data;
	node->prev = NULL;
	node->next = list->head;

	if (list->head)
		list->head->prev = node;

	list->head = node;

	if (list->tail == NULL)
		list->tail = node;

	list->size++;

	return;
}
/**************************************************************************************************************************/
static void DLinkedListDupUnsafe(DLinkedList *list, DLinkedList *ret_list)
{
	DLinkedListNode *node;
	DLinkedListNode *new_node;

	for (node = list->head; node; node = node->next)
	{
		new_node = calloc(1, sizeof(DLinkedListNode));
		new_node->data = node->data;
		DLinkedListAdd(ret_list, new_node, new_node->data);

		continue;
	}

	return;
}
/**************************************************************************************************************************/
static void DLinkedListDupFilterUnsafe(DLinkedList *list, DLinkedList *ret_list, DLinkedListFilterNode *filter_func, char *filter_key, char *filter_value)
{
	DLinkedListNode *node;
	DLinkedListNode *new_node;
	int filter_node;

	for (node = list->head; node; node = node->next)
	{
		if (!filter_func)
			continue;

		filter_node = filter_func(node, filter_key, filter_value);

		/* filter node */
		if (filter_node)
			continue;

		/* copy node */
		new_node = calloc(1, sizeof(DLinkedListNode));
		new_node->data = node->data;
		DLinkedListAddUnsafe(ret_list, new_node, new_node->data);
	}

	return;
}
/**************************************************************************************************************************/
static void DLinkedListSortMergeUnsafe(DLinkedList *list, DLinkedListCompareFunc *cmp_func, DLinkedListSortCodes cmp_flag)
{
	DLinkedListNode *node;

	/* Return empty list */
	if (!list->head)
		return;

	/* check if the return list has more than one item */
	if (list->size > 1)
	{
		/* call the function to order */
		node 			= list->head;
		list->head 		= DLinkedListMergeSortFunc(node, cmp_func, cmp_flag);

		node			= list->head;

		while(node->next)
			node		= node->next;

		list->tail 		= node;
	}

	return;
}
/**************************************************************************************************************************/
