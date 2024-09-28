/*
 * api_error.c
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
BrbJsonObject *BrbJsonErrorReplyInit(void)
{
	BrbJsonObject *root_node  	= BrbJsonObjectInit();
	BrbJsonValue *error_arr		= BrbJsonValueInitArray();

	BrbJsonObjectAdd(root_node, "success", BrbJsonValueInitBoolean(0));
	BrbJsonObjectAdd(root_node, "errors", error_arr);

	return root_node;
}
/**********************************************************************************************************************/
int BrbJsonErrorReplyDestroy(BrbJsonObject *root_node)
{
	/* Sanitize */
	if (!root_node)
		return JSON_FAILURE;

	/* Free BrbJsonArray */
	BrbJsonObjectFree(root_node);

	return JSON_SUCCESS;
}
/**********************************************************************************************************************/
long BrbJsonErrorReplyErrorsCount(BrbJsonObject *root_node)
{
	if (!root_node)
		return 0;

	return BrbJsonArrayGetCount(BrbJsonObjectGetArray(root_node, "errors"));
}
/**********************************************************************************************************************/
int BrbJsonErrorReplyAddFmt(BrbJsonObject *root_node, const char *id_str, const char *msg_str, ...)
{
	char msg_buf[65535];
	va_list args;
	int op_status;
	int msg_sz;

	/* Generate Error Text */
	va_start(args, msg_str);
	msg_sz = vsnprintf((char*) &msg_buf, (65535 - 1), msg_str, args);
	msg_buf[msg_sz] = '\0';
	va_end(args);

	/* Add JSON Error */
	op_status 	= BrbJsonErrorReplyAdd(root_node, id_str, (char *)&msg_buf);

	return op_status;
}
/**********************************************************************************************************************/
int BrbJsonErrorReplyAdd(BrbJsonObject *root_node, const char *id_str, const char *msg_str)
{
	BrbJsonValue *error_json;
	BrbJsonValue *json_value_aux;
	BrbJsonArray *error_json_arr;

	if (!root_node)
		return JSON_FAILURE;

	char *id_str_cp = malloc(strlen(id_str)+1);
	char *msg_str_cp = malloc(strlen(msg_str)+1);

	strcpy(id_str_cp, id_str);
	strcpy(msg_str_cp, msg_str);

	error_json_arr 	= BrbJsonObjectDotGetArray(root_node, "errors");

	error_json  	= BrbJsonValueInitObject();

	json_value_aux 	= BrbJsonValueInitString(id_str_cp);
	BrbJsonObjectAdd(BrbJsonValueGetObject(error_json), "id", json_value_aux);

	json_value_aux 	= BrbJsonValueInitString(msg_str_cp);
	BrbJsonObjectAdd(BrbJsonValueGetObject(error_json), "msg", json_value_aux);

	/* Add into Array */
	BrbJsonArrayAdd(error_json_arr, error_json);

	return JSON_SUCCESS;
}
/**********************************************************************************************************************/
