/*
 * linked_list.c
 *
 *  Created on: 2014-09-10
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

/**************************************************************************************************************************/
LinkedList *LinkedListNew (void)
{
	LinkedList *new;
	BRB_CALLOC(new, 1, sizeof(LinkedList));

	/* Sanity check */
	if (!new)
		return 0;

	new->data = NULL;
	new->next = NULL;
	return new;
}
/**************************************************************************************************************************/
void *LinkedListGetData (LinkedList *elem)
{
	if (!elem)
		return NULL;

	return elem->data;
}
/**************************************************************************************************************************/
int LinkedListHasNext (LinkedList *list)
{
	if (!list)
		return 0;

	return (list->next != NULL);
}
/**************************************************************************************************************************/
LinkedList *LinkedListInsertTail(LinkedList *list, void *data)
{
	LinkedList *elem = LinkedListNew();

	/* Failed to create new node */
	if (!elem)
		return list;

	elem->data = data;
	LinkedList *aux;

	if (!list)
		return elem;

	else
	{
		aux = list;
		while (aux->next)
			aux = aux->next;
		aux->next = elem;
	}
	return list;
}
/**************************************************************************************************************************/
LinkedList *LinkedListInsertHead(LinkedList *list, void *data)
{
	LinkedList *elem = LinkedListNew();

	elem->data = data;
	elem->next = list;
	return elem;
}
/**************************************************************************************************************************/
LinkedList *LinkedListNext (LinkedList *elem)
{
	if (!elem)
		return NULL;

	return elem->next;
}
/**************************************************************************************************************************/
LinkedList *LinkedListRemove (LinkedList *list, void *data, equalFunction equalityFunc)
{
	LinkedList *aux = list;
	LinkedList *behind = NULL;
	void *tmpPtr;

	if (!list)
		return NULL;

	while (aux && !((equalityFunc)(data, aux->data)))
	{
		behind = aux;
		aux = aux->next;
	}

	if (aux) { // encontrou!

		if (behind) {
			behind->next = aux->next;
			BRB_FREE(aux);
		}
		else {
			tmpPtr = aux->next;
			BRB_FREE(aux);
			return tmpPtr;
		}
	}
	return list;
}
/**************************************************************************************************************************/
int LinkedListExist (LinkedList *list, void *data, equalFunction eq)
{
	LinkedList *aux = list;
	int result = 0;

	while (aux && !(eq)(LinkedListGetData(aux),data))
		aux = LinkedListNext(aux);

	if (aux)
		result = 1;

	return result;
}
/**************************************************************************************************************************/
int LinkedListGetSize (LinkedList *list)
{
	int n = 0;
	LinkedList *aux = list;

	while (aux)
	{
		aux = aux->next;
		n++;
	}
	return n;
}
/**************************************************************************************************************************/
LinkedList *LinkedListMerge (LinkedList *head, LinkedList *tail)
{
	LinkedList *aux = head;
	if (!head)
		return tail;

	while (aux->next)
		aux = aux->next;

	aux->next = tail;
	return head;
}
/**************************************************************************************************************************/
void LinkedListDestroy (LinkedList *list)
{
	LinkedList *aux;

	while (list)
	{
		aux = list;
		list = list->next;
		BRB_FREE(aux);
	}
}
/**************************************************************************************************************************/
void LinkedListNodeDestroy (LinkedList *list)
{
	BRB_FREE(list);
}
/**************************************************************************************************************************/
static LinkedList *LinkedListInsertAfter (LinkedList *lst, void *data)
{

	if (!lst)
		return LinkedListInsertHead(lst,data);

	LinkedList *aux = lst->next;

	lst->next = LinkedListNew();
	lst->next->next = aux;
	lst->next->data = data;
	return lst;
}
/**************************************************************************************************************************/
LinkedList *LinkedListSort (LinkedList *lst, equalFunction f, int desc)
{
	LinkedList *res = NULL;
	LinkedList *tmp,*grd = lst;
	LinkedList *aux1,*aux2,*aux = grd;

	if (!aux)
		return NULL;

	res = LinkedListInsertHead(res,LinkedListGetData(aux));
	grd = aux;
	aux = aux->next;
	int insert;
	int cmp = (desc) ? -1 : 1;
	BRB_FREE(grd);

	while (aux)
	{
		tmp = res; aux1 = NULL; insert = 0;
		while (tmp&&!insert)
		{
			if ((f)(LinkedListGetData(aux),LinkedListGetData(tmp)) == cmp)
			{
				// inserir aki!
				if (!aux1) {
					res = LinkedListInsertHead(res,LinkedListGetData(aux));
				}
				else {
					aux1 = LinkedListInsertAfter(aux1,LinkedListGetData(aux));
				}
				insert = 1;
			}
			aux1 = tmp;
			tmp = tmp->next;
		}
		if (!insert)
		{
			aux1 = LinkedListInsertTail(aux1,LinkedListGetData(aux));
		}
		grd = aux;
		aux = aux->next;
		BRB_FREE(grd);
	}
	return res;
}
/**************************************************************************************************************************/
