/*
 * api_object.c
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
BrbJsonValue *BrbJsonObjectGetValue(const BrbJsonObject *object, const char *name)
{
	return BrbJsonObjectNgetValue(object, name, strlen(name));
}
/**********************************************************************************************************************/
BrbJsonObject *BrbJsonObjectGetObject(const BrbJsonObject *object, const char *name)
{
	return BrbJsonValueGetObject(BrbJsonObjectGetValue(object, name));
}
/**********************************************************************************************************************/
BrbJsonArray *BrbJsonObjectGetArray(const BrbJsonObject *object, const char *name)
{
	return BrbJsonValueGetArray(BrbJsonObjectGetValue(object, name));
}
/**********************************************************************************************************************/
const char * BrbJsonObjectGetString(const BrbJsonObject *object, const char *name)
{
	return BrbJsonValueGetString(BrbJsonObjectGetValue(object, name));
}
/**********************************************************************************************************************/
double BrbJsonObjectGetNumber(const BrbJsonObject *object, const char *name)
{
	return BrbJsonValueGetNumber(BrbJsonObjectGetValue(object, name));
}
/**********************************************************************************************************************/
int BrbJsonObjectGetBoolean(const BrbJsonObject *object, const char *name)
{
	return BrbJsonValueGetBoolean(BrbJsonObjectGetValue(object, name));
}
/**********************************************************************************************************************/
BrbJsonValue *BrbJsonObjectDotGetValue(const BrbJsonObject *object, const char *name)
{
	const char *dot_position = strchr(name, '.');

	if (!dot_position)
		return BrbJsonObjectGetValue(object, name);

	object 	= BrbJsonValueGetObject(BrbJsonObjectNgetValue(object, name, dot_position - name));

	return BrbJsonObjectDotGetValue(object, dot_position + 1);
}
/**********************************************************************************************************************/
const char *BrbJsonObjectDotGetString(const BrbJsonObject *object, const char *name)
{
	return BrbJsonValueGetString(BrbJsonObjectDotGetValue(object, name));
}
/**********************************************************************************************************************/
double BrbJsonObjectDotGetNumber(const BrbJsonObject *object, const char *name)
{
	return BrbJsonValueGetNumber(BrbJsonObjectDotGetValue(object, name));
}
/**********************************************************************************************************************/
BrbJsonObject *BrbJsonObjectDotGetObject(const BrbJsonObject *object, const char *name)
{
	return BrbJsonValueGetObject(BrbJsonObjectDotGetValue(object, name));
}
/**********************************************************************************************************************/
BrbJsonArray *BrbJsonObjectDotGetArray(const BrbJsonObject *object, const char *name)
{
	return BrbJsonValueGetArray(BrbJsonObjectDotGetValue(object, name));
}
/**********************************************************************************************************************/
int BrbJsonObjectDotGetBoolean(const BrbJsonObject *object, const char *name)
{
	return BrbJsonValueGetBoolean(BrbJsonObjectDotGetValue(object, name));
}
/**********************************************************************************************************************/
long BrbJsonObjectGetCount(const BrbJsonObject *object)
{
	return object ? object->count : 0;
}
/**********************************************************************************************************************/
const char * BrbJsonObjectGetName(const BrbJsonObject *object, long index)
{
	if (index >= BrbJsonObjectGetCount(object))
		return NULL;

	return object->names[index];
}
/**********************************************************************************************************************/
/* Print Methods */
/**********************************************************************************************************************/
void BrbJsonObjectPrintMemBuffer(MemBuffer *json_mb, BrbJsonObject *object)
{
	int i;
	int has_comma = 0;

	MemBufferPrintf(json_mb, "{");

	for (i = 0; i < BrbJsonObjectGetCount(object); i++)
	{
		if (has_comma)
		{
			MemBufferPrintf(json_mb, ",");
		}
		has_comma = 1;

		BrbJsonValuePrintMemBuffer(json_mb, object->names[i], object->values[i]);

	}

	MemBufferPrintf(json_mb, "}");

	return;

}
/**********************************************************************************************************************/
