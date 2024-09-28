/*
 * api_value.c
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

/**********************************************************************************************************************/
BrbJsonValueType BrbJsonValueGetType(const BrbJsonValue *value)
{
	return value ? value->type : JSON_ERROR;
}
/**********************************************************************************************************************/
BrbJsonObject *BrbJsonValueGetObject(const BrbJsonValue *value)
{
	return BrbJsonValueGetType(value) == JSON_OBJECT ? value->value.object : NULL;
}
/**********************************************************************************************************************/
BrbJsonArray *BrbJsonValueGetArray(const BrbJsonValue *value)
{
	return BrbJsonValueGetType(value) == JSON_ARRAY ? value->value.array : NULL;
}
/**********************************************************************************************************************/
const char *BrbJsonValueGetString(const BrbJsonValue *value)
{
	return BrbJsonValueGetType(value) == JSON_STRING ? value->value.string : NULL;
}
/**********************************************************************************************************************/
int BrbJsonValueGetStrSize(const BrbJsonValue *value)
{
	return BrbJsonValueGetType(value) == JSON_STRING ? strlen(value->value.string) : 0;
}
/**********************************************************************************************************************/
double BrbJsonValueGetNumber(const BrbJsonValue *value)
{
	return BrbJsonValueGetType(value) == JSON_NUMBER ? value->value.number : 0;
}
/**********************************************************************************************************************/
int BrbJsonValueGetBoolean(const BrbJsonValue *value)
{
	return BrbJsonValueGetType(value) == JSON_BOOLEAN ? value->value.boolean : -1;
}
/**********************************************************************************************************************/
void BrbJsonValueFree(BrbJsonValue *value)
{
	/* sanitize */
	if (!value)
		return;

	switch (BrbJsonValueGetType(value))
	{
	case JSON_OBJECT:
		BrbJsonObjectFree(value->value.object);
		break;
	case JSON_STRING:
		if (value->value.string)
		{
			BrbJsonFree(value->value.string);
		}
		break;
	case JSON_ARRAY:
		BrbJsonArrayFree(value->value.array);
		break;
	default:
		break;
	}
	BrbJsonFree(value);

	return;
}
/**********************************************************************************************************************/
/* Print Methods */
/**********************************************************************************************************************/
void BrbJsonValuePrintMemBuffer(MemBuffer *json_mb, const char *json_key, BrbJsonValue *value)
{
	/* Sanitize */
	if (!json_mb || !json_key || !value)
		return;

	if (*json_key != '\0')
	{
		MemBufferPrintf(json_mb, "\"%s\": ", json_key);
	}

	switch (BrbJsonValueGetType(value))
	{
	case JSON_OBJECT:
		BrbJsonObjectPrintMemBuffer(json_mb, value->value.object);
		break;
	case JSON_ARRAY:
		BrbJsonArrayPrintMemBuffer(json_mb, value->value.array);
		break;
	case JSON_NULL:
		MemBufferPrintf(json_mb, "null");
		break;
	case JSON_NUMBER:
		if (ceil(value->value.number) == floor(value->value.number))
			MemBufferPrintf(json_mb, "%ld", (long)value->value.number);
		else
			MemBufferPrintf(json_mb, "%.2f", value->value.number);
		break;
	case JSON_STRING:
//		MemBufferPrintf(json_mb, "\"%s\"", value->value.string);
		MemBufferAdd(json_mb, "\"", 1);
		BrbJsonMemBufferAddSlashed(json_mb, value->value.string, -1, -1);
		MemBufferAdd(json_mb, "\"", 1);
		break;
	case JSON_BOOLEAN:
		MemBufferPrintf(json_mb, "%s", (value->value.boolean ? "true" : "false"));
		break;
	default:
		break;
	}

	return;
}
/**********************************************************************************************************************/
