/*
 * api_array.c
 *
 *  Created on: 2014-04-03
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

#include "../libbrb_json.h"

/**********************************************************************************************************************/
BrbJsonValue * BrbJsonArrayGetValue(const BrbJsonArray *array, long index)
{
	if (index >= BrbJsonArrayGetCount(array))
		return NULL;

	return array->items[index];
}
/**********************************************************************************************************************/
const char * BrbJsonArrayGetString(const BrbJsonArray *array, long index)
{
	return BrbJsonValueGetString(BrbJsonArrayGetValue(array, index));
}
/**********************************************************************************************************************/
double BrbJsonArrayGetNumber(const BrbJsonArray *array, long index)
{
	return BrbJsonValueGetNumber(BrbJsonArrayGetValue(array, index));
}
/**********************************************************************************************************************/
BrbJsonObject * BrbJsonArrayGetObject(const BrbJsonArray *array, long index)
{
	return BrbJsonValueGetObject(BrbJsonArrayGetValue(array, index));
}
/**********************************************************************************************************************/
BrbJsonArray * BrbJsonArrayGetArray(const BrbJsonArray *array, long index)
{
	return BrbJsonValueGetArray(BrbJsonArrayGetValue(array, index));
}
/**********************************************************************************************************************/
int BrbJsonArrayGetBoolean(const BrbJsonArray *array, long index)
{
	return BrbJsonValueGetBoolean(BrbJsonArrayGetValue(array, index));
}
/**********************************************************************************************************************/
long BrbJsonArrayGetCount(const BrbJsonArray *array)
{
	return array ? array->count : 0;
}
/**********************************************************************************************************************/
/* Print Methods */
/**********************************************************************************************************************/
void BrbJsonArrayPrintMemBuffer(MemBuffer *json_mb, BrbJsonArray *array)
{
	BrbJsonValue *item_arr;

	int i;
	int has_comma = 0;

	MemBufferPrintf(json_mb, "[");

	for (i = 0; i < BrbJsonArrayGetCount(array); i++)
	{

		item_arr = BrbJsonArrayGetValue(array, i);

		if (has_comma)
		{
			MemBufferPrintf(json_mb, ",");
		}

		has_comma = 1;

		BrbJsonValuePrintMemBuffer(json_mb, "", item_arr);

	}

	MemBufferPrintf(json_mb, "]");

	return;
}
/**********************************************************************************************************************/
