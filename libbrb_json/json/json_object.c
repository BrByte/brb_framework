/*
 * json_object.c
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
BrbJsonObject *BrbJsonObjectInit(void)
{
	BrbJsonObject *new_obj = (BrbJsonObject*) BrbJsonMalloc(sizeof(BrbJsonObject));

	/* sanitize */
	if (!new_obj)
		return NULL;

	new_obj->names 		= (const char**) NULL;
	new_obj->values 	= (BrbJsonValue**) NULL;
	new_obj->capacity 	= 0;
	new_obj->count 		= 0;

	return new_obj;
}
/**********************************************************************************************************************/
int BrbJsonObjectAdd(BrbJsonObject *object, const char *name, BrbJsonValue *value)
{
	long new_capacity;
	long index;

	/* Check capacity */
	if (object->count >= object->capacity)
	{
		new_capacity 	= MAX(object->capacity * 2, JSON_INITIAL_CAPACITY);

		if (new_capacity > JSON_OBJECT_MAX_CAPACITY)
			return JSON_FAILURE;

		if (BrbJsonObjectResize(object, new_capacity) == JSON_FAILURE)
			return JSON_FAILURE;
	}

	if (BrbJsonObjectGetValue(object, name) != NULL)
		return JSON_FAILURE;

	index 					= object->count;
	object->names[index] 	= BrbJsonStrNDup(name, strlen(name));

	if (!object->names[index])
		return JSON_FAILURE;

	object->values[index] 	= value;
	object->count++;

	return JSON_SUCCESS;
}
/**********************************************************************************************************************/
int BrbJsonObjectResize(BrbJsonObject *object, long capacity)
{
	if (BrbJsonTryRealloc((void**) &object->names, capacity * sizeof(char*)) == JSON_FAILURE)
		return JSON_FAILURE;

	if (BrbJsonTryRealloc((void**) &object->values, capacity * sizeof(BrbJsonValue*)) == JSON_FAILURE)
		return JSON_FAILURE;

	object->capacity = capacity;

	return JSON_SUCCESS;
}
/**********************************************************************************************************************/
BrbJsonValue *BrbJsonObjectNgetValue(const BrbJsonObject *object, const char *name, long n)
{
	long i, name_length;

	/* sanitize */
	if (!object)
		return NULL;

	for (i = 0; i < BrbJsonObjectGetCount(object); i++)
	{

		name_length 	= strlen(object->names[i]);

		if (name_length != n)
			continue;

		if (strncmp(object->names[i], name, n) == 0)
			return object->values[i];

		continue;
	}

	return NULL;
}
/**********************************************************************************************************************/
int BrbJsonObjectFree(BrbJsonObject *object)
{
	/* sanitize */
	if (!object)
		return JSON_FAILURE;

	while (object->count--)
	{
		BrbJsonFree(object->names[object->count]);
		BrbJsonValueFree(object->values[object->count]);
	}

	BrbJsonFree(object->names);
	BrbJsonFree(object->values);
	BrbJsonFree(object);

	return JSON_SUCCESS;
}
/**********************************************************************************************************************/
