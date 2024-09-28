/*
 * libbrb_json.h
 *
 *  Created on: 2013-09-08
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

#ifndef LIBBRB_JSON_H_
#define LIBBRB_JSON_H_

/* BRB Framework */
#include <libbrb_core.h>

/**********************************************************************************************************************/
/* DEFINES */
/************************************************************/
#define JSON_INITIAL_CAPACITY       20
#define JSON_ARRAY_MAX_CAPACITY    	163840 	/* 20 * (2^13)8192 */
#define JSON_OBJECT_MAX_CAPACITY 	1280 	/* 20 * (2^6)  */
#define JSON_MAX_NESTING            20

#define sizeof_token(a)       	(sizeof(a) - 1)
#define skip_char(str)        	((*str)++)
#define skip_whitespaces(str) 	while (isspace(**str)) { skip_char(str); }

#define BrbJsonMalloc(a)     	malloc(a)
#define BrbJsonFree(a)       	free((void*)a)
#define BrbJsonRealloc(a, b) 	realloc(a, b)
/**********************************************************************************************************************/
/* ENUMS */
/************************************************************/
typedef enum
{
	JSON_FAILURE   	= -1,
	JSON_SUCCESS 	= 0,
} BrbJsonStatusCode;
/************************************************************/
typedef enum
{
    JSON_ERROR   = 0,
    JSON_NULL    = 1,
    JSON_STRING  = 2,
    JSON_NUMBER  = 3,
    JSON_OBJECT  = 4,
    JSON_ARRAY   = 5,
    JSON_BOOLEAN = 6,
    JSON_INTEGER = 7
} BrbJsonValueType;
/**********************************************************************************************************************/
/* STRUCTS */
/************************************************************/
typedef union _BrbJsonValue_value
{
	const char *string;
	double number;
	struct _BrbJsonObject *object;
	struct _BrbJsonArray *array;
	int boolean;
	int null;
} BrbJsonValue_Value;
/************************************************************/
typedef struct _BrbJsonValue
{
	BrbJsonValueType type;
	BrbJsonValue_Value value;
} BrbJsonValue;
/************************************************************/
typedef struct _BrbJsonObject
{
	const char **names;
	BrbJsonValue **values;
	long count;
	long capacity;
} BrbJsonObject;
/************************************************************/
typedef struct _BrbJsonArray
{
	BrbJsonValue **items;
	long count;
	long capacity;
} BrbJsonArray;
/**********************************************************************************************************************/
/* JSON Parses first JSON value in a file, returns NULL in case of error */
/**********************************************************************************************************************/
BrbJsonValue *BrbJsonParseFile(char *filename);
BrbJsonValue *BrbJsonParseString(const char *string);
BrbJsonValue *BrbJsonParseMemBuffer(MemBuffer *mb);
BrbJsonValue *BrbJsonParseFileWithComments(char *filename);
BrbJsonValue *BrbJsonParseStringWithComments(const char *string);
/**********************************************************************************************************************/
BrbJsonValue *BrbJsonParseConfigFile(char *filename);
BrbJsonValue *BrbJsonParseConfigMemBuffer(MemBuffer *file_mb);
BrbJsonValue *BrbJsonParseConfigString(char *buffer_ptr, unsigned long buffer_sz);
/**********************************************************************************************************************/
/* JSON JSON Object */
/**********************************************************************************************************************/
BrbJsonObject *BrbJsonObjectInit	  (void);

BrbJsonValue  *BrbJsonObjectGetValue  (const BrbJsonObject *object, const char *name);
BrbJsonObject *BrbJsonObjectGetObject (const BrbJsonObject *object, const char *name);
BrbJsonArray  *BrbJsonObjectGetArray  (const BrbJsonObject *object, const char *name);
const char    *BrbJsonObjectGetString (const BrbJsonObject *object, const char *name);
double         BrbJsonObjectGetNumber (const BrbJsonObject *object, const char *name);
int            BrbJsonObjectGetBoolean(const BrbJsonObject *object, const char *name);

int 		   BrbJsonObjectAdd		  (BrbJsonObject *object, const char *name, BrbJsonValue *value);
int 		   BrbJsonObjectResize	  (BrbJsonObject *object, long capacity);
BrbJsonValue  *BrbJsonObjectNgetValue (const BrbJsonObject *object, const char *name, long n);
int 		   BrbJsonObjectFree	  (BrbJsonObject *object);
/**********************************************************************************************************************/
/* JSON DotGet functions enable addressing values with dot notation in nested objects
 * (e.g. objectA.objectB.attribute).
 * Because valid names in JSON can contain dots, some values may be inaccessible this way.
 **/
/**********************************************************************************************************************/
BrbJsonValue  *BrbJsonObjectDotGetValue  (const BrbJsonObject *object, const char *name);
BrbJsonObject *BrbJsonObjectDotGetObject (const BrbJsonObject *object, const char *name);
BrbJsonArray  *BrbJsonObjectDotGetArray  (const BrbJsonObject *object, const char *name);
const char    *BrbJsonObjectDotGetString (const BrbJsonObject *object, const char *name);
double         BrbJsonObjectDotGetNumber (const BrbJsonObject *object, const char *name);
int            BrbJsonObjectDotGetBoolean(const BrbJsonObject *object, const char *name);
/**********************************************************************************************************************/
/* JSON Functions to get available names */
/**********************************************************************************************************************/
long        BrbJsonObjectGetCount(const BrbJsonObject *object);
const char *BrbJsonObjectGetName (const BrbJsonObject *object, long index);
/**********************************************************************************************************************/
/* JSON Array */
/**********************************************************************************************************************/
BrbJsonArray  *BrbJsonArrayInit		 (void);

BrbJsonValue  *BrbJsonArrayGetValue  (const BrbJsonArray *array, long index);
BrbJsonObject *BrbJsonArrayGetObject (const BrbJsonArray *array, long index);
BrbJsonArray  *BrbJsonArrayGetArray  (const BrbJsonArray *array, long index);
const char    *BrbJsonArrayGetString (const BrbJsonArray *array, long index);
double         BrbJsonArrayGetNumber (const BrbJsonArray *array, long index);
int            BrbJsonArrayGetBoolean(const BrbJsonArray *array, long index);
long           BrbJsonArrayGetCount  (const BrbJsonArray *array);

int 		  BrbJsonArrayAdd	(BrbJsonArray *array, BrbJsonValue *value);
int 		  BrbJsonArrayResize(BrbJsonArray *array, long capacity);
int 		  BrbJsonArrayFree	(BrbJsonArray *array);
/**********************************************************************************************************************/
/* JSON Value */
/**********************************************************************************************************************/
BrbJsonValue 	*BrbJsonValueInitObject	(void);
BrbJsonValue 	*BrbJsonValueInitArray	(void);
BrbJsonValue 	*BrbJsonValueInitString	(const char *string);
BrbJsonValue 	*BrbJsonValueInitNumber	(double number);
BrbJsonValue 	*BrbJsonValueInitBoolean(int boolean);
BrbJsonValue 	*BrbJsonValueInitNull	(void);
BrbJsonValue    *BrbJsonValueAddValue   (BrbJsonValue *js_root, char *key_ptr, BrbJsonValue *new_val);

BrbJsonValueType BrbJsonValueGetType  (const BrbJsonValue *value);
BrbJsonObject	*BrbJsonValueGetObject  (const BrbJsonValue *value);
BrbJsonArray    *BrbJsonValueGetArray   (const BrbJsonValue *value);
const char      *BrbJsonValueGetString  (const BrbJsonValue *value);
int      	     BrbJsonValueGetStrSize (const BrbJsonValue *value);
double           BrbJsonValueGetNumber  (const BrbJsonValue *value);
int              BrbJsonValueGetBoolean (const BrbJsonValue *value);
void             BrbJsonValueFree       (BrbJsonValue *value);
/**********************************************************************************************************************/
/* JSON ErrorReply */
/**********************************************************************************************************************/
BrbJsonObject *BrbJsonErrorReplyInit(void);
int BrbJsonErrorReplyDestroy(BrbJsonObject *root_node);
int BrbJsonErrorReplyAddFmt(BrbJsonObject *root_node, const char *id_str, const char *msg_str, ...);
int BrbJsonErrorReplyAdd(BrbJsonObject *root_node, const char *id_str, const char *msg_str);
long BrbJsonErrorReplyErrorsCount(BrbJsonObject *root_node);
/**********************************************************************************************************************/
void BrbJsonValuePrintMemBuffer	 (MemBuffer *json_mb, const char *json_key, BrbJsonValue *value);
void BrbJsonObjectPrintMemBuffer (MemBuffer *json_mb, BrbJsonObject *object);
void BrbJsonArrayPrintMemBuffer	 (MemBuffer *json_mb, BrbJsonArray *array);
int BrbJsonTryRealloc(void **ptr, long new_size);
char *BrbJsonStrNDup(const char *string, long n);
/**********************************************************************************************************************/
/* JSON MemBuffer Utility */
/**********************************************************************************************************************/
int BrbJsonMemBufferAddKeySlashed(MemBuffer *mb, char *key_str, char *buf_str);
int BrbJsonMemBufferAddKeySlashedSize(MemBuffer *mb, char *key_str, char *buf_str, int buf_sz);
int BrbJsonMemBufferAddKeySlashedLimitedFmt(MemBuffer *mb, char *key_str, int lmt_sz, char *buf_str, ...);
int BrbJsonMemBufferAddKeySlashedLimited(MemBuffer *mb, char *key_str, char *buf_str, int buf_sz, int lmt_sz);
int BrbJsonMemBufferAddSlashed(MemBuffer *mb, char *buf_str, int buf_sz, int lmt_sz);

typedef int BrbJsonDumpDLinkedNode(MemBuffer *json_reply_mb, void *data_ptr, void *cb_data);
int BrbJsonDumpDLinkedList(DLinkedList *list, MemBuffer *json_mb, int off_start, int off_limit, BrbJsonDumpDLinkedNode *cb_func, void *cb_data);
int BrbJsonDumpReplyList(DLinkedList *list, MemBuffer *json_mb, int off_start, int off_limit, BrbJsonDumpDLinkedNode *cb_func, void *cb_data);
/**********************************************************************************************************************/
#endif
