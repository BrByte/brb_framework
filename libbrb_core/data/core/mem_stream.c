/*
 * mem_stream.c
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

/**************************************************************************************************************************/
MemStreamNode *MemStreamNodeNew(MemStream *mem_stream, unsigned long node_sz)
{
	MemStreamNode *mem_node;
	unsigned long loc_node_sz;

	if ( (mem_stream->min_node_sz > 0) && (node_sz < mem_stream->min_node_sz) )
	{
		//printf("MemStreamNodeNew - Forcing node_sz of [%lu] to min_node_sz of [%ld]\n", node_sz, mem_stream->min_node_sz);
		loc_node_sz = mem_stream->min_node_sz;
	}
	else
	{
		//printf("MemStreamNodeNew - Asked node size of [%lu] and min_node_sz is [%ld]\n", node_sz, mem_stream->min_node_sz);
		loc_node_sz = node_sz;
	}

	/* Create the new mem_node */
	mem_node				= calloc(1, sizeof(MemStreamNode));
	mem_node->node_data		= calloc(1, loc_node_sz);
	mem_node->node_cap		= loc_node_sz;

	/* Save parent stream reference inside node */
	mem_node->parent_stream = mem_stream;

	/* Increment node count */
	mem_stream->nodes.node_count++;

	return mem_node;
}
/**************************************************************************************************************************/
int MemStreamNodeDestroy(MemStream *mem_stream, MemStreamNode *mem_node_ptr)
{
	if (!mem_node_ptr)
		return 0;

	if (mem_node_ptr->node_refcount > 0)
	{
		mem_node_ptr->node_refcount--;
		return 0;
	}
	else
	{
		/* Destroy node */
		free(mem_node_ptr->node_data);
		free(mem_node_ptr);

		mem_node_ptr = NULL;

		/* Decrement node count */
		mem_stream->nodes.node_count--;

		return 1;
	}

	return 0;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
MemStream *MemStreamNew(unsigned long min_node_sz, MemStreamType type)
{
	MemStream *mem_stream;

	/* Create mem_stream and set node size */
	mem_stream				= calloc(1, sizeof(MemStream));
	mem_stream->min_node_sz = min_node_sz;
	mem_stream->stream_type = type;

	MEMSTREAM_MUTEX_INIT(mem_stream);

	return mem_stream;
}
/**************************************************************************************************************************/
void MemStreamClean(MemStream *mem_stream)
{
	MemStreamNode *first_mem_node;
	MemStreamNode *cur_mem_node;
	MemStreamNode *next_mem_node;

	MEMSTREAM_MUTEX_LOCK(mem_stream);

	/* Leave first mem_node */
	first_mem_node	= mem_stream->nodes.head_ptr;
	cur_mem_node	= first_mem_node->next_node;

	/* Empty stream */
	if (!mem_stream->nodes.node_count)
	{
		MEMSTREAM_MUTEX_UNLOCK(mem_stream);
		return;
	}

	/* Has nodes, free all of them */
	if (mem_stream->nodes.node_count > 1)
	{
		do
		{
			/* Grab next mem_node, if any */
			next_mem_node = cur_mem_node->next_node;

			/* Destroy the node */
			MemStreamNodeDestroy(mem_stream, cur_mem_node);

		} while(cur_mem_node = next_mem_node);
	}

	/* Leave just one node */
	mem_stream->nodes.node_count	= 1;
	mem_stream->data_size			= 0;
	mem_stream->data_off			= 0;

	/* Clean the first mem_node */
	memset(first_mem_node->node_data, 0, first_mem_node->node_sz);

	/* Reset and clean first node */
	first_mem_node->node_sz		= 0;
	first_mem_node->node_off	= 0;

	/* Leave pointing to first node */
	mem_stream->nodes.head_ptr = mem_stream->nodes.cur_ptr = mem_stream->nodes.tail_ptr = first_mem_node;

	MEMSTREAM_MUTEX_UNLOCK(mem_stream);

	return;
}
/**************************************************************************************************************************/
void MemStreamDestroy(MemStream *mem_stream)
{
	MemStreamNode *cur_mem_node;
	MemStreamNode *next_mem_node;
	int destroy_count = 0;

	/* Sanity check */
	if (!mem_stream)
		return;

	MEMSTREAM_MUTEX_LOCK(mem_stream);

	cur_mem_node = mem_stream->nodes.head_ptr;

	/* Empty stream */
	if (!mem_stream->nodes.node_count)
	{
		free(mem_stream);

		MEMSTREAM_MUTEX_UNLOCK(mem_stream);

		return;
	}

	/* Has nodes, free all of them */
	do
	{
		//	printf("MemStreamDestroy - Destroyed index [%d] - ADDR [%p] - SIZE [%ld]\n", destroy_count, cur_mem_node, cur_mem_node->node_sz);

		/* Grab next mem_node, if any */
		next_mem_node = cur_mem_node->next_node;

		/* Destroy first node */
		MemStreamNodeDestroy(mem_stream, cur_mem_node);

		destroy_count++;

	} while( (cur_mem_node = next_mem_node) != NULL);

	MEMSTREAM_MUTEX_UNLOCK(mem_stream);

	free(mem_stream);

	return;
}
/**************************************************************************************************************************/
int MemStreamCreateNewNodeTail(MemStream *mem_stream, unsigned long data_sz)
{
	MemStreamNode *new_mem_node;
	MemStreamNode *tail_mem_node;

	MEMSTREAM_MUTEX_LOCK(mem_stream);

	/* This stream is empty, create first node */
	if (!mem_stream->nodes.node_count)
	{
		/* Create the first stream node */
		mem_stream->nodes.head_ptr = mem_stream->nodes.cur_ptr = mem_stream->nodes.tail_ptr = MemStreamNodeNew(mem_stream, data_sz);

		MEMSTREAM_MUTEX_UNLOCK(mem_stream);

		return 0;

	}
	/* Multi_node stream, create new node */
	else
	{
		/* Create a new mem_node */
		new_mem_node	= MemStreamNodeNew(mem_stream, data_sz);
		tail_mem_node	= mem_stream->nodes.tail_ptr;

		/* Current tail_node NEXT will point to newly created node */
		tail_mem_node->next_node = new_mem_node;

		/* NEW tail_node PREV will point to current tail */
		new_mem_node->prev_node = mem_stream->nodes.tail_ptr;

		/* Update node_list tail_ptr */
		mem_stream->nodes.tail_ptr = new_mem_node;
	}

	MEMSTREAM_MUTEX_UNLOCK(mem_stream);

	return mem_stream->nodes.node_count;
}
/**************************************************************************************************************************/
int MemStreamGrabDataFromFD(MemStream *mem_stream, unsigned long data_sz, int fd)
{
	MemStreamNode *mem_node;
	char *dst_base_ptr;

	unsigned long orig_off;
	unsigned long data_left;

	unsigned long read_bytes = 0;

	MEMSTREAM_MUTEX_LOCK(mem_stream);

	/* This stream is empty, create first node */
	if (!mem_stream->nodes.node_count)
	{
		/* Create a new tail node */
		MEM_NODE_NEW_TAILNODE(mem_stream, mem_node, data_sz);

		/* Get node write base */
		MEM_NODE_GET_CUR_BASE(mem_node, dst_base_ptr);

		/* Copy buffer from kernel */
		read_bytes += read(fd, dst_base_ptr, data_sz);

		/* Update node and stream counters */
		COUNTER_UPDATE_NODE_AND_STREAM(mem_stream, mem_node, read_bytes);
	}
	/* This stream has nodes, work on it */
	else
	{
		/* Grab last mem_node */
		mem_node = mem_stream->nodes.tail_ptr;

		/* Check if node has capacity avaiable */
		if (data_sz < MEM_NODE_AVAILCAP(mem_node))
		{
			/* Get node write base */
			MEM_NODE_GET_CUR_BASE(mem_node, dst_base_ptr);

			/* Copy buffer from kernel */
			read_bytes += read(fd, dst_base_ptr, data_sz);

			/* Update node and stream counters */
			COUNTER_UPDATE_NODE_AND_STREAM(mem_stream, mem_node, read_bytes);
		}
		/* Node has just partial cap */
		else
		{
			orig_off	= MEM_NODE_AVAILCAP(mem_node);

			/* Get node write base */
			MEM_NODE_GET_CUR_BASE(mem_node, dst_base_ptr);

			/* Copy buffer from kernel */
			read_bytes += read(fd, dst_base_ptr, orig_off);

			/* Update node and stream counters */
			COUNTER_UPDATE_NODE_AND_STREAM(mem_stream, mem_node, read_bytes);

			/* Calculate data_left */
			data_left	= data_sz - read_bytes;

			/* Create a new tail node */
			MEM_NODE_NEW_TAILNODE(mem_stream, mem_node, data_left);

			/* Get node write base */
			MEM_NODE_GET_CUR_BASE(mem_node, dst_base_ptr);

			/* Copy buffer from kernel */
			read_bytes += read(fd, dst_base_ptr, data_left);

			/* Update node and stream counters */
			COUNTER_UPDATE_NODE_AND_STREAM(mem_stream, mem_node, data_left);
		}

	}

	MEMSTREAM_MUTEX_UNLOCK(mem_stream);

	return read_bytes;
}
/**************************************************************************************************************************/
void MemStreamWrite(MemStream *mem_stream, void *data, unsigned long data_sz)
{
	MemStreamNode *mem_node;

	char *dst_base_ptr;
	char *orig_base_ptr;

	unsigned long orig_off;
	unsigned long data_left;

	MEMSTREAM_MUTEX_LOCK(mem_stream);

	/* This stream is empty, create first node */
	if (!mem_stream->nodes.node_count)
	{
		/* Create a new tail node */
		MEM_NODE_NEW_TAILNODE(mem_stream, mem_node, data_sz);

		/* Copy data into node */
		MEM_NODE_COPYINTO(mem_node, data, data_sz);

		/* Update node and stream counters */
		COUNTER_UPDATE_NODE_AND_STREAM(mem_stream, mem_node, data_sz);
	}
	/* This stream has nodes, work on it */
	else
	{
		/* Grab last mem_node */
		mem_node = mem_stream->nodes.tail_ptr;

		/* Check if node has capacity avaiable */
		if (data_sz < MEM_NODE_AVAILCAP(mem_node))
		{
			/* Copy data into node */
			MEM_NODE_COPYINTO(mem_node, data, data_sz);

			/* Update node and stream counters */
			COUNTER_UPDATE_NODE_AND_STREAM(mem_stream, mem_node, data_sz);
		}
		/* Node has just partial cap */
		else
		{
			orig_off	= MEM_NODE_AVAILCAP(mem_node);
			data_left	= data_sz - orig_off;

			/* Copy data into node */
			MEM_NODE_COPYINTO(mem_node, data, orig_off);

			/* Update node and stream counters */
			COUNTER_UPDATE_NODE_AND_STREAM(mem_stream, mem_node, orig_off);

			/* Create a new tail node */
			MEM_NODE_NEW_TAILNODE(mem_stream, mem_node, data_left);

			/* Calculate new base origin */
			orig_base_ptr = data;
			orig_base_ptr += orig_off;

			/* Copy partial data left into node */
			MEM_NODE_COPYINTO(mem_node, orig_base_ptr, data_left);

			/* Update node and stream counters */
			COUNTER_UPDATE_NODE_AND_STREAM(mem_stream, mem_node, data_left);
		}

	}

	MEMSTREAM_MUTEX_UNLOCK(mem_stream);

	return;
}
/**************************************************************************************************************************/
void MemStreamWriteToFILE(MemStream *mem_stream, FILE *file)
{
	MemStreamNode *mem_node;
	int i;

	MEMSTREAM_MUTEX_LOCK(mem_stream);

	for (i = 0, mem_node = mem_stream->nodes.head_ptr; mem_node; mem_node = mem_node->next_node, i++)
	{
		fprintf(file, "NODE [%d] - ADDR [%p] - DATA_SZ [%ld] - DATA [%s]\n", i, mem_node, mem_node->node_sz, (char *)mem_node->node_data);
	}

	MEMSTREAM_MUTEX_UNLOCK(mem_stream);

	return;
}
/**************************************************************************************************************************/
void MemStreamWriteToFD(MemStream *mem_stream, int fd)
{
	MemStreamNode *mem_node;
	int op_status;

	MEMSTREAM_MUTEX_LOCK(mem_stream);

	for (mem_node = mem_stream->nodes.head_ptr; mem_node; mem_node = mem_node->next_node)
		op_status = write(fd, mem_node->node_data, mem_node->node_sz);

	MEMSTREAM_MUTEX_UNLOCK(mem_stream);

	return;
}
/**************************************************************************************************************************/
void MemStreamOffsetDeref(MemStream *mem_stream, MemStreamRef *mem_stream_ref, unsigned long offset)
{
	MemStreamNode *mem_node;
	unsigned long cur_off;
	int i;

	MEMSTREAM_MUTEX_LOCK(mem_stream);

	for ( (i = 0, mem_node = mem_stream->nodes.head_ptr, cur_off = mem_node->node_sz); mem_node; (mem_node = mem_node->next_node, i++, cur_off += mem_node->node_sz))
	{
		if (cur_off >= offset)
		{
			//printf("MemStreamOffsetDeref - Found node at idx [%d] for offset [%lu] - cur_off [%lu]\n", i, offset, cur_off);

			mem_stream_ref->node_idx	= i;

			mem_stream_ref->data		= mem_node->node_data;
			mem_stream_ref->node_base	= mem_node->node_data;
			mem_stream_ref->node_sz		= mem_node->node_sz;

			MEMSTREAM_MUTEX_UNLOCK(mem_stream);

			return;
		}
		else
		{
			//printf("MemStreamOffsetDeref - idx [%d] - Requested offset [%lu], we are at [%lu]\n", i, offset, cur_off);
		}
	}

	MEMSTREAM_MUTEX_UNLOCK(mem_stream);

	return;
}
/**************************************************************************************************************************/
void MemStreamBaseDeref(MemStream *mem_stream, MemStreamRef *mem_stream_ref)
{
	MemStreamNode *head_ptr;

	MEMSTREAM_MUTEX_LOCK(mem_stream);

	head_ptr = mem_stream->nodes.head_ptr;

	mem_stream_ref->node_idx	= 0;

	mem_stream_ref->data		= head_ptr->node_data;
	mem_stream_ref->node_base	= head_ptr->node_data;
	mem_stream_ref->node_sz		= head_ptr->node_sz;

	MEMSTREAM_MUTEX_UNLOCK(mem_stream);

	return;

}
/**************************************************************************************************************************/
unsigned long MemStreamGetDataSize(MemStream *mem_stream)
{
	MEMSTREAM_MUTEX_LOCK(mem_stream);

	return mem_stream->data_size;

	MEMSTREAM_MUTEX_UNLOCK(mem_stream);
}
/**************************************************************************************************************************/
unsigned long MemStreamGetNodeCount(MemStream *mem_stream)
{
	MEMSTREAM_MUTEX_LOCK(mem_stream);

	return mem_stream->nodes.node_count;

	MEMSTREAM_MUTEX_UNLOCK(mem_stream);
}
/**************************************************************************************************************************/

