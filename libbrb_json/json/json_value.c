/*
 * json_value.c
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
BrbJsonValue * BrbJsonValueInitObject(void)
{
	BrbJsonValue *new_value 	= (BrbJsonValue*) BrbJsonMalloc(sizeof(BrbJsonValue));

	/* Sanitize */
	if (!new_value)
		return NULL;

	new_value->type 			= JSON_OBJECT;
	new_value->value.object 	= BrbJsonObjectInit();

	if (!new_value->value.object)
	{
		BrbJsonFree(new_value);
		return NULL;
	}

	return new_value;
}
/**********************************************************************************************************************/
BrbJsonValue *BrbJsonValueInitArray(void)
{
	BrbJsonValue *new_value 	= (BrbJsonValue*) BrbJsonMalloc(sizeof(BrbJsonValue));

	/* Sanitize */
	if (!new_value)
		return NULL;

	new_value->type 			= JSON_ARRAY;
	new_value->value.array 		= BrbJsonArrayInit();

	if (!new_value->value.array)
	{
		BrbJsonFree(new_value);
		return NULL;
	}

	return new_value;
}
/**********************************************************************************************************************/
BrbJsonValue *BrbJsonValueInitString(const char *string)
{
	BrbJsonValue *new_value 	= (BrbJsonValue*) BrbJsonMalloc(sizeof(BrbJsonValue));

	/* Sanitize */
	if (!new_value)
		return NULL;

	new_value->type 			= JSON_STRING;
	new_value->value.string 	= string;

	return new_value;
}
/**********************************************************************************************************************/
BrbJsonValue *BrbJsonValueInitNumber(double number)
{
	BrbJsonValue *new_value 	= (BrbJsonValue*) BrbJsonMalloc(sizeof(BrbJsonValue));

	/* Sanitize */
	if (!new_value)
		return NULL;
	new_value->type 			= JSON_NUMBER;
	new_value->value.number 	= number;

	return new_value;
}
/**********************************************************************************************************************/
BrbJsonValue *BrbJsonValueInitBoolean(int boolean)
{
	BrbJsonValue *new_value 	= (BrbJsonValue*) BrbJsonMalloc(sizeof(BrbJsonValue));

	/* Sanitize */
	if (!new_value)
		return NULL;
	new_value->type 			= JSON_BOOLEAN;
	new_value->value.boolean 	= boolean;

	return new_value;
}
/**********************************************************************************************************************/
BrbJsonValue *BrbJsonValueInitNull(void)
{
	BrbJsonValue *new_value 	= (BrbJsonValue*) BrbJsonMalloc(sizeof(BrbJsonValue));

	/* Sanitize */
	if (!new_value)
		return NULL;
	new_value->type 			= JSON_NULL;

	return new_value;
}
/**************************************************************************************************************************/
// Add values in a dot like structure
BrbJsonValue *BrbJsonValueAddValue(BrbJsonValue *js_root, char *key_ptr, BrbJsonValue *new_val)
{
	BrbJsonValue *json_cur 		= js_root;
	BrbJsonValue *json_val 		= js_root;
	BrbJsonValue *json_aux 		= NULL;
	int cur_index;
	char *tokenName 			= key_ptr;
	char *dot_ptr;
	char *bracket;

	do
	{
		tokenName 				= key_ptr;
		dot_ptr 				= strchr(tokenName, '.');
		bracket 				= strchr(tokenName, '[');
		cur_index 				= 0;

		if (dot_ptr)
		{
			if (dot_ptr < bracket)
				bracket 		= NULL;

			dot_ptr[0] 			= '\0';
			key_ptr 			= dot_ptr + 1;
		}

		if (bracket != NULL)
		{
			bracket[0] 			= '\0';
			bracket++;

			// Extract the array index
			cur_index 			= bracket[0] != ']' ? atoi(bracket + 1) : -1;
		}

        /* Has token, we need at least one object */
        if (tokenName[0] != '\0')
		{
			if (json_cur->type == JSON_ARRAY)
			{
				/* Need to create a new object */
				json_val 		= BrbJsonArrayGetValue(BrbJsonValueGetArray(json_cur), 0);

				/* Has object */
				if (json_val)
				{
					/* Check for key inside */
					json_aux 	= BrbJsonObjectGetValue(BrbJsonValueGetObject(json_val), tokenName);

					/* Found current key, reset to create another object */
					if (json_aux)
						json_val 	= NULL;
				}

				if (!json_val)
				{
					json_val 	= BrbJsonValueInitObject();
					BrbJsonArrayAdd(BrbJsonValueGetArray(json_cur), json_val);
				}

				json_cur 		= json_val;
			}

			json_val 			= BrbJsonObjectGetValue(BrbJsonValueGetObject(json_cur), tokenName);
		}

		/* last item */
		if (!dot_ptr)
		{
			if (!new_val)
			{
				if (bracket != NULL)
				{
					new_val 	= BrbJsonValueInitArray();
				}
				else
				{
					new_val 	= BrbJsonValueInitObject();
				}
			}

			if (json_cur->type == JSON_ARRAY)
			{
				BrbJsonArrayAdd(BrbJsonValueGetArray(json_cur), new_val);
			}
			else
			{
				BrbJsonObjectAdd(BrbJsonValueGetObject(json_cur), tokenName, new_val);
			}

			return new_val;
		}

		if (json_val)
			goto next_child;

		if (bracket != NULL)
		{
			json_val 	= BrbJsonValueInitArray();
		}
		else
		{
			json_val 	= BrbJsonValueInitObject();
		}

		if (json_cur->type == JSON_ARRAY)
		{
			BrbJsonArrayAdd(BrbJsonValueGetArray(json_cur), json_val);
		}
		else
		{
			BrbJsonObjectAdd(BrbJsonValueGetObject(json_cur), tokenName, json_val);
		}

		next_child:

		json_cur 			= json_val;
		json_val 			= NULL;
		continue;

	} while (dot_ptr != NULL);

	return json_cur;
}
/**********************************************************************************************************************/
