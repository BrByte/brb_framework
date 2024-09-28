/*
 * json_array.c
 *
 *  Created on: 2014-04-04
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

#ifndef MAX
#define MAX(a, b)             ((a) > (b) ? (a) : (b))
#endif
/**********************************************************************************************************************/
BrbJsonArray *BrbJsonArrayInit(void)
{
	BrbJsonArray *new_array = (BrbJsonArray*) BrbJsonMalloc(sizeof(BrbJsonArray));

	/* Sanitize */
	if (!new_array)
		return NULL;

	new_array->items 	= (BrbJsonValue**) NULL;
	new_array->capacity = 0;
	new_array->count 	= 0;

	return new_array;
}
/**********************************************************************************************************************/
int BrbJsonArrayAdd(BrbJsonArray *array, BrbJsonValue *value)
{
	if (array->count >= array->capacity)
	{
		long new_capacity = MAX(array->capacity * 2, JSON_INITIAL_CAPACITY);

		if (new_capacity > JSON_ARRAY_MAX_CAPACITY)
			return JSON_FAILURE;

		if (BrbJsonArrayResize(array, new_capacity) != JSON_SUCCESS)
			return JSON_FAILURE;
	}

	array->items[array->count] = value;
	array->count++;

	return JSON_SUCCESS;
}
/**********************************************************************************************************************/
int BrbJsonArrayResize(BrbJsonArray *array, long capacity)
{
	if (BrbJsonTryRealloc((void**) &array->items, capacity * sizeof(BrbJsonValue*)) == JSON_FAILURE)
		return JSON_FAILURE;

	array->capacity = capacity;

	return JSON_SUCCESS;
}
/**********************************************************************************************************************/
int BrbJsonArrayFree(BrbJsonArray *array)
{
	/* sanitize */
	if (!array)
		return JSON_FAILURE;

	while (array->count--)
		BrbJsonValueFree(array->items[array->count]);

	BrbJsonFree(array->items);
	BrbJsonFree(array);

	return JSON_SUCCESS;
}
/**********************************************************************************************************************/
