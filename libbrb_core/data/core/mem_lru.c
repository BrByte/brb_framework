/*
 * mem_lru.c
 *
 *  Created on: 2022-03-21
 *      Author: Dev3 <carlos@brbyte.com>
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
void MemSlotLRU_Init(MemslotLRU *mem_lru, EvKQBase *ev_base, unsigned int data_sz, unsigned int max_sz)
{
	mem_lru->ev_base			= ev_base;

	mem_lru->data_sz			= data_sz;
	mem_lru->max_sz				= max_sz;

	/* Initialize MEMSLOT */
	MemSlotBaseInit(&mem_lru->memslot, (mem_lru->data_sz + sizeof(MemslotLRUData)), mem_lru->max_sz + 1, BRBDATA_THREAD_UNSAFE);
}
/**************************************************************************************************************************/
void MemSlotLRU_Clear(MemslotLRU *mem_lru)
{
	MemslotLRUData *data		= NULL;

	/* Sanitize */
	if (!mem_lru)
		return;

	/* Check Limit */
	while (mem_lru->memslot.list[0].size > 0)
	{
		data					= (MemslotLRUData *)MemSlotBaseSlotPopHead(&mem_lru->memslot, 0);

		/* Sanitize */
		if (!data)
			break;

		data->flags.in_use		= 0;
	}

	mem_lru->cur_id				= 0;
}
/**************************************************************************************************************************/
void MemSlotLRU_CheckLimit(MemslotLRU *mem_lru)
{
	MemslotLRUData *data	= NULL;

	/* Sanitize */
	if (!mem_lru)
		return;

	/* Check if Limited */
	if (mem_lru->max_sz == 0)
		return;

	/* Check Limit */
	while (mem_lru->memslot.list[0].size > mem_lru->max_sz)
	{
		data					= (MemslotLRUData *)MemSlotBaseSlotPopHead(&mem_lru->memslot, 0);

		/* Sanitize */
		if (!data)
			break;

		data->flags.in_use		= 0;
	}
}
/**************************************************************************************************************************/
MemslotLRUData *MemSlotLRU_Create(MemslotLRU *mem_lru)
{
	MemslotLRUData *data	= NULL;
	struct tm *cur_time 	= localtime((time_t*)&mem_lru->ev_base->stats.cur_invoke_tv.tv_sec);

	/* Sanitize */
	if (!mem_lru)
		return NULL;

	/* Create new one */
	data 					= MemSlotBaseSlotGrab(&mem_lru->memslot);

	/* Sanitize */
	if (!data)
		return NULL;

	/* Reset info */
	memset(data, 0, (mem_lru->data_sz + sizeof(MemslotLRUData)));

	data->slot_id			= MemSlotBaseSlotGetID(data);
	data->flags.in_use		= 1;
	data->id 				= ++mem_lru->cur_id;

	/* Set date */
	snprintf((char*)&data->date_str, (sizeof(data->date_str) - 1), "%04d-%02d-%02d %02d:%02d:%02d",
			cur_time->tm_year + 1900, cur_time->tm_mon + 1, cur_time->tm_mday,
			cur_time->tm_hour, cur_time->tm_min, cur_time->tm_sec);

	MemSlotLRU_CheckLimit(mem_lru);

	return data;
}
/**************************************************************************************************************************/
