/*
 * queue.c
 *
 *  Created on: 2013-02-22
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

/**************************************************************************************************************************/
QueueList *queueListNew(QueueMTState queue_type)
{
	QueueList *queue = calloc(1, sizeof(QueueList));

	queueListInit(queue, queue_type);

	return queue;
}
/**************************************************************************************************************************/
void queueListDestroy(QueueList *queue)
{
	QueueData *queue_data;

	if (!queue)
		return;

	QUEUE_MUTEX_LOCK(queue);

	/* Destroy all enqueued queue_data */
	while (queue_data = (QueueData*)DLinkedListPopHead(&queue->list))
		queueDataDestroy(queue_data);

	queue->list_size	= 0;

	QUEUE_MUTEX_UNLOCK(queue);

	QUEUE_MUTEX_DESTROY(queue);

	free(queue);

	return;
}
/**************************************************************************************************************************/
void queueListClean(QueueList *queue)
{
	QueueData *queue_data;

	if (!queue)
		return;

	QUEUE_MUTEX_LOCK(queue);

	/* Destroy all enqueued queue_data */
	while (queue_data = (QueueData*)DLinkedListPopHead(&queue->list))
		queueDataDestroy(queue_data);

	queue->list_size	= 0;

	QUEUE_MUTEX_UNLOCK(queue);

	return;
}
/**************************************************************************************************************************/
void queueListInit(QueueList *queue, QueueMTState queue_type)
{
	/* Define MT_SAFETY */
	if (QUEUE_MT_SAFE == queue_type)
		queue->flags.mt_engine	= 1;
	else
		queue->flags.mt_engine	= 0;

	queue->list_size	= 0;

	QUEUE_MUTEX_INIT(queue);

	return;
}
/**************************************************************************************************************************/
int queueListIsEmpty(QueueList *queue)
{
	/* Nothing to check for, bail out */
	QUEUE_MUTEX_LOCK(queue);

	if (DLINKED_LIST_ISEMPTY(queue->list))
	{
		QUEUE_MUTEX_UNLOCK(queue);
		return 1;
	}

	QUEUE_MUTEX_UNLOCK(queue);

	return 0;
}
/**************************************************************************************************************************/
void queueDataEnqueue(QueueList *queue, QueueData *queue_data, QueueSemPost post)
{
	QUEUE_MUTEX_LOCK(queue);
	DLinkedListAddTail(&queue->list, &queue_data->node, queue_data);
	queue->list_size++;
	QUEUE_MUTEX_UNLOCK(queue);

	if (QUEUE_DO_POST == post)
		sem_post(&queue->queue_semaphore);

	return;
}
/**************************************************************************************************************************/
QueueData * queueDataDequeue(QueueList *queue)
{
	QueueData *queue_data;

	/* Nothing to check for, bail out */
	QUEUE_MUTEX_LOCK(queue);

	if (DLINKED_LIST_ISEMPTY(queue->list))
	{
		QUEUE_MUTEX_UNLOCK(queue);
		return NULL;
	}

	queue_data = (QueueData*)DLinkedListPopHead(&queue->list);
	queue->list_size--;
	QUEUE_MUTEX_UNLOCK(queue);

	return queue_data;
}
/**************************************************************************************************************************/
QueueData *queueDataNew(void *data, QueueDataDestroyFunc *destroy_func, QueueDataCBH *finish_cb, void *finish_cbdata)
{
	QueueData *queue_data;

	/* Create new queue_data */
	queue_data = calloc(1, sizeof(QueueData));

	/* Populate data */
	queue_data->data = data;
	queue_data->destroy_func	= destroy_func;
	queue_data->finish_cb		= finish_cb;
	queue_data->finish_cbdata	= finish_cbdata;

	return queue_data;
}
/**************************************************************************************************************************/
void queueDataDestroy(QueueData *queue_data)
{
	QueueDataDestroyFunc *destroy_func;

	/* Sanity check */
	if (!queue_data)
		return;

	/* Invoke destroy function, if any */
	if (queue_data->destroy_func)
	{
//		printf("queueItemDestroy - Destroying ptr at [%p]\n", queue_data->data);
		queue_data->destroy_func(queue_data->data);
	}

	free(queue_data);

	return;
}
/**************************************************************************************************************************/
