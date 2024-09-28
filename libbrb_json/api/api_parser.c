/*
 * api_parser.c
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
/* Parser */
/**********************************************************************************************************************/
static const char *BrbJsonGetProcessedString(const char **string);
static BrbJsonValue *BrbJsonParseObjectValue(const char **string, long nesting);
static BrbJsonValue *BrbJsonParseArrayValue(const char **string, long nesting);
static BrbJsonValue *BrbJsonParseStringValue(const char **string);
static BrbJsonValue *BrbJsonParseBooleanValue(const char **string);
static BrbJsonValue *BrbJsonParseNumberValue(const char **string);
static BrbJsonValue *BrbJsonParseNullValue(const char **string);
static BrbJsonValue *BrbJsonParseValue(const char **string, long nesting);

/**********************************************************************************************************************/
/* Utils */
/**********************************************************************************************************************/
static void remove_comments(char *string, const char *start_token, const char *end_token);
static void skip_quotes(const char **str);
static int BrbStrIsUTF(const unsigned char *str);
static int BrbStrIsDecimal(const char *str, long length);
/**********************************************************************************************************************/
/* Parser API */
/**********************************************************************************************************************/
BrbJsonValue *BrbJsonParseFile(char *filename)
{
	BrbJsonValue *json_val 	= NULL;
	MemBuffer *file_mb;

	file_mb 				= MemBufferReadFromFile(filename);

	if (!file_mb)
		return NULL;

	json_val 				= BrbJsonParseMemBuffer(file_mb);

	MemBufferDestroy(file_mb);

	return json_val;
}
/**********************************************************************************************************************/
BrbJsonValue *BrbJsonParseFileWithComments(char *filename)
{
	BrbJsonValue *json_val 	= NULL;
	MemBuffer *file_mb;

	file_mb 				= MemBufferReadFromFile(filename);

	if (!file_mb)
		return NULL;

	json_val 				= BrbJsonParseStringWithComments(MemBufferDeref(file_mb));

	MemBufferDestroy(file_mb);

	return json_val;
}
/**********************************************************************************************************************/
BrbJsonValue *BrbJsonParseMemBuffer(MemBuffer *mb)
{
	if (!mb)
		return NULL;

	return BrbJsonParseString(MemBufferDeref(mb));
}
/**********************************************************************************************************************/
BrbJsonValue *BrbJsonParseString(const char *string)
{
	if (!string)
		return NULL;

	BRB_SKIP_WHITESPACES(string);

	if ((*string != '{' && *string != '['))
		return NULL;

	return BrbJsonParseValue((const char**) &string, 0);
}
/**********************************************************************************************************************/
BrbJsonValue *BrbJsonParseStringWithComments(const char *string)
{
	BrbJsonValue *result 		= NULL;
	char *string_mutable_copy 	= NULL, *string_mutable_copy_ptr = NULL;
	string_mutable_copy 		= BrbJsonStrNDup(string, strlen(string));

	if (!string_mutable_copy)
		return NULL;

	remove_comments(string_mutable_copy, "/*", "*/");
	remove_comments(string_mutable_copy, "//", "\n");

	string_mutable_copy_ptr 	= string_mutable_copy;

	skip_whitespaces(&string_mutable_copy_ptr);

	if (*string_mutable_copy_ptr != '{' && *string_mutable_copy_ptr != '[')
	{
		BrbJsonFree(string_mutable_copy);
		return NULL;
	}

	result 			= BrbJsonParseValue((const char**) &string_mutable_copy_ptr, 0);
	BrbJsonFree(string_mutable_copy);

	return result;
}
/**********************************************************************************************************************/
/* Parser */
/**********************************************************************************************************************/
static const char *BrbJsonGetProcessedString(const char **string)
{
	const char *string_start = *string;
	char *output, *processed_ptr, *unprocessed_ptr, current_char;
	unsigned int utf_val;

	skip_quotes(string);

	if (**string == '\0')
		return NULL;

	output 			= BrbJsonStrNDup(string_start + 1, *string - string_start - 2);

	if (!output)
		return NULL;
	processed_ptr 	= unprocessed_ptr = output;

	while (*unprocessed_ptr)
	{
		current_char = *unprocessed_ptr;
		if (current_char == '\\')
		{
			unprocessed_ptr++;
			current_char = *unprocessed_ptr;
			switch (current_char)
			{
			case '\"':
			case '\\':
			case '/':
				break;
			case 'b':
				current_char = '\b';
				break;
			case 'f':
				current_char = '\f';
				break;
			case 'n':
				current_char = '\n';
				break;
			case 'r':
				current_char = '\r';
				break;
			case 't':
				current_char = '\t';
				break;
			case 'u':
				unprocessed_ptr++;
				if (!BrbStrIsUTF((const unsigned char*) unprocessed_ptr) || sscanf(unprocessed_ptr, "%4x", &utf_val) == EOF)
				{
					BrbJsonFree(output);
					return NULL;
				}
				if (utf_val < 0x80)
				{
					current_char = utf_val;
				}
				else if (utf_val < 0x800)
				{
					*processed_ptr++ = (utf_val >> 6) | 0xC0;
					current_char = ((utf_val | 0x80) & 0xBF);
				}
				else
				{
					*processed_ptr++ = (utf_val >> 12) | 0xE0;
					*processed_ptr++ = (((utf_val >> 6) | 0x80) & 0xBF);
					current_char = ((utf_val | 0x80) & 0xBF);
				}
				unprocessed_ptr += 3;
				break;
			default:
				BrbJsonFree(output);
				return NULL;
				break;
			}
		}
		else if ((unsigned char) current_char < 0x20)
		{ /* 0x00-0x19 are invalid characters for json string (http://www.ietf.org/rfc/rfc4627.txt) */
			BrbJsonFree(output);
			return NULL;
		}

		*processed_ptr = current_char;
		processed_ptr++;
		unprocessed_ptr++;
	}

	*processed_ptr = '\0';

	if (BrbJsonTryRealloc((void**) &output, strlen(output) + 1) == JSON_FAILURE)
		return NULL;

	return output;
}
/**********************************************************************************************************************/
static BrbJsonValue *BrbJsonParseValue(const char **string, long nesting)
{
	if (nesting > JSON_MAX_NESTING)
		return NULL;

	skip_whitespaces(string);

	switch (**string)
	{
	case '{':
		return BrbJsonParseObjectValue(string, nesting + 1);
	case '[':
		return BrbJsonParseArrayValue(string, nesting + 1);
	case '\"':
		return BrbJsonParseStringValue(string);
	case 'f':
	case 't':
		return BrbJsonParseBooleanValue(string);
	case '-':
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		return BrbJsonParseNumberValue(string);
	case 'n':
		return BrbJsonParseNullValue(string);
	default:
		return NULL;
	}
}
/**********************************************************************************************************************/
static BrbJsonValue *BrbJsonParseObjectValue(const char **string, long nesting)
{
	BrbJsonValue *output_value = BrbJsonValueInitObject(), *new_value = NULL;
	BrbJsonObject *output_object = BrbJsonValueGetObject(output_value);

	const char *new_key = NULL;

	if (!output_value)
		return NULL;

	skip_char(string);
	skip_whitespaces(string);

	if (**string == '}')
	{ /* empty object */
		skip_char(string);
		return output_value;
	}

	while (**string != '\0')
	{

		new_key = BrbJsonGetProcessedString(string);
		skip_whitespaces(string);

		if (!new_key || **string != ':')
		{
			BrbJsonValueFree(output_value);
			return NULL;
		}

		skip_char(string);
		new_value = BrbJsonParseValue(string, nesting);

		if (!new_value)
		{
			BrbJsonFree(new_key);
			BrbJsonValueFree(output_value);
			return NULL;
		}

		if (BrbJsonObjectAdd(output_object, new_key, new_value) != JSON_SUCCESS)
		{
			BrbJsonFree(new_key);
			BrbJsonFree(new_value);
			BrbJsonValueFree(output_value);
			return NULL;
		}

		BrbJsonFree(new_key);
		skip_whitespaces(string);

		if (**string != ',')
			break;

		skip_char(string);
		skip_whitespaces(string);
	}

	skip_whitespaces(string);

	/* Trim object after parsing is over */
	if (**string != '}' || BrbJsonObjectResize(output_object, BrbJsonObjectGetCount(output_object)) == JSON_FAILURE)
	{
		BrbJsonValueFree(output_value);
		return NULL;
	}

	skip_char(string);

	return output_value;
}
/**********************************************************************************************************************/
static BrbJsonValue *BrbJsonParseArrayValue(const char **string, long nesting)
{
	BrbJsonValue *output_value = BrbJsonValueInitArray();
	BrbJsonValue *new_array_value = NULL;

	BrbJsonArray *output_array = BrbJsonValueGetArray(output_value);

	if (!output_value)
		return NULL;

	skip_char(string);
	skip_whitespaces(string);

	if (**string == ']')
	{ /* empty array */
		skip_char(string);
		return output_value;
	}

	while (**string != '\0')
	{
		new_array_value = BrbJsonParseValue(string, nesting);
		if (!new_array_value)
		{
			BrbJsonValueFree(output_value);
			return NULL;
		}

		if (BrbJsonArrayAdd(output_array, new_array_value) == JSON_FAILURE)
		{
			BrbJsonFree(new_array_value);
			BrbJsonValueFree(output_value);
			return NULL;
		}
		skip_whitespaces(string);

		if (**string != ',')
			break;

		skip_char(string);
		skip_whitespaces(string);
	}

	skip_whitespaces(string);

	/* Trim array after parsing is over */
	if (**string != ']' || BrbJsonArrayResize(output_array, BrbJsonArrayGetCount(output_array)) == JSON_FAILURE)
	{
		BrbJsonValueFree(output_value);
		return NULL;
	}

	skip_char(string);
	return output_value;
}
/**********************************************************************************************************************/
static BrbJsonValue *BrbJsonParseStringValue(const char **string)
{
	const char *new_string = BrbJsonGetProcessedString(string);

	if (!new_string)
		return NULL;

	return BrbJsonValueInitString(new_string);
}
/**********************************************************************************************************************/
static BrbJsonValue *BrbJsonParseBooleanValue(const char **string)
{
	long true_token_size 	= sizeof_token("true");
	long false_token_size 	= sizeof_token("false");

	if (strncmp("true", *string, true_token_size) == 0)
	{
		*string 	+= true_token_size;
		return BrbJsonValueInitBoolean(1);
	}
	else if (strncmp("false", *string, false_token_size) == 0)
	{
		*string 	+= false_token_size;
		return BrbJsonValueInitBoolean(0);
	}

	return NULL;
}
/**********************************************************************************************************************/
static BrbJsonValue *BrbJsonParseNumberValue(const char **string)
{
	char *end;
	double number = strtod(*string, &end);
	BrbJsonValue *output_value;

	//TODO: Separate INT/DOUBLE. Reason: BrbSQLJSONWhereToSqlSafeString 3244883 -> 3.24488e+06
	if (BrbStrIsDecimal(*string, end - *string))
	{
		*string 		= end;
		output_value 	= BrbJsonValueInitNumber(number);
	}
	else
	{
		output_value = NULL;
	}

	return output_value;
}
/**********************************************************************************************************************/
static BrbJsonValue *BrbJsonParseNullValue(const char **string)
{
	long token_size = sizeof_token("null");

	if (strncmp("null", *string, token_size) == 0)
	{
		*string 	+= token_size;
		return BrbJsonValueInitNull();
	}

	return NULL;
}
/**************************************************************************************************************************/
BrbJsonValue *BrbJsonParseConfigFile(char *filename)
{
	BrbJsonValue *json_val 	= NULL;
	MemBuffer *file_mb 		= NULL;

	/* Sanitize */
	if (!filename)
		return NULL;

	/* Read File */
	file_mb 				= MemBufferReadFromFile(filename);

	/* Sanitize */
	if (!file_mb)
		return NULL;

	json_val 				= BrbJsonParseConfigMemBuffer(file_mb);

	/* Release Buffer */
	MemBufferDestroy(file_mb);

	return json_val;
}
/**************************************************************************************************************************/
BrbJsonValue *BrbJsonParseConfigMemBuffer(MemBuffer *file_mb)
{
	BrbJsonValue *json_val = NULL;

	/* Sanitize */
	if (!file_mb || MemBufferGetSize(file_mb) <= 0)
		return NULL;

	json_val 				= BrbJsonParseConfigString(MemBufferDeref(file_mb), MemBufferGetSize(file_mb));

	return json_val;
}
/**************************************************************************************************************************/
BrbJsonValue *BrbJsonParseConfigString(char *buffer_ptr, unsigned long buffer_sz)
{
    // Create a JSON object to represent the parsed data
    BrbJsonValue *js_root		= BrbJsonValueInitObject();
    BrbJsonValue *js_cur_val 	= js_root;
    BrbJsonValue *js_new_val 	= js_root;

	/* Sanitize */
	if (!js_root)
		return NULL;

    // Initialize a stack to keep track of nested structures
    // Assuming a reasonable maximum nesting depth
    BrbJsonValue *stack_val[64] 	= {0};
    int stack_sz 					= 0;

    char *line_ptr 		= buffer_ptr;
    char *end_ptr 		= NULL;
    int line_sz 		= 0;
    int indent_level 	= 0;
    int dot_index 		= 0;
    int sep_index 		= 0;

    /* Zero fill info */
    memset(&stack_val, 0, sizeof(stack_val));

    /* Iterate string */
    while (*line_ptr != '\0')
    {
        indent_level 	= 0;
        line_sz 		= 0;
        dot_index 		= 0;
        sep_index 		= 0;

        // Skip the newline character
        while (*line_ptr == '\n' || *line_ptr == '\r')
            line_ptr++;

        // Calculate the indentation level
        while (*line_ptr == ' ' || *line_ptr == '\t')
        {
        	indent_level++;
        	line_ptr++;
        }

        // Calculate the size of the line_ptr (up to the newline character)
        while (line_ptr[line_sz] != '\0' && line_ptr[line_sz] != '\n')
        {
        	if (dot_index == 0 && line_ptr[line_sz] == '.')
        		dot_index 	= line_sz;

        	if (sep_index == 0 && (line_ptr[line_sz] == ' ' || line_ptr[line_sz] == '='))
        		sep_index 	= line_sz;

            line_sz++;
        }

        end_ptr 		= line_ptr + line_sz;
        BRBCFG_TRIM_BACK(line_ptr, line_sz);

        line_ptr[line_sz] 	= '\0';

        if (line_sz < 1)
        	goto next_line;

        if (line_ptr[0] == '#')
        	goto next_line;

        if (sep_index <= 0)
            sep_index 		= line_sz;

        if (line_ptr[0] == '!')
        {
            // End of a block, pop the stack
            stack_sz--;
            if (stack_sz > 0)
            	stack_sz--;

            js_cur_val 			= stack_val[stack_sz];
            js_new_val 			= js_cur_val;

        }
        else
        {
            char *dot_ptr;
            char *val_ptr;
            int val_sz;

            // Check if the line_ptr represents an array element
            dot_ptr 			= line_ptr + dot_index;

            val_ptr 			= line_ptr + sep_index;
            val_sz 				= line_sz - sep_index;

            BRBCFG_SKIP_ZEROED(val_ptr, val_sz);

            if (val_sz <= 0)
            	val_ptr 		= "";

            // Handle different indentation levels
            if (indent_level > stack_sz)
            {
            	stack_val[stack_sz] = js_cur_val;
                stack_sz++;

                // A nested block, push to the stack
            	stack_val[stack_sz] = js_new_val;
                stack_sz++;
                js_cur_val 			= js_new_val;
            }
            else if (indent_level < stack_sz)
            {
                // Pop the stack until the indentation level matches
                while (stack_sz > indent_level)
                {
                	BrbJsonValue *pop_val 	= js_cur_val;
                    stack_sz--;
                	js_cur_val 				= stack_val[stack_sz];
                }
            }

        	if (val_ptr[0] != '\0')
        	{
            	js_new_val 		= BrbJsonValueInitString(strdup(val_ptr));
        	}
        	else
        	{
            	js_new_val 		= NULL;
        	}

        	js_new_val 			= BrbJsonValueAddValue(js_cur_val, line_ptr, js_new_val);
        }

        next_line:

        // Move to the next line_ptr
        line_ptr 				= end_ptr + 1;
        continue;
    }

    return js_root;
}
/**********************************************************************************************************************/
/* UTILS */
/**********************************************************************************************************************/
static void remove_comments(char *string, const char *start_token, const char *end_token)
{
	int in_string = 0, escaped = 0;
	long i;
	char *ptr 				= NULL, current_char;
	long start_token_len 	= strlen(start_token);
	long end_token_len 		= strlen(end_token);

	if (start_token_len == 0 || end_token_len == 0)
		return;

	while ((current_char = *string) != '\0')
	{
		if (current_char == '\\' && !escaped)
		{
			escaped = 1;
			string++;
			continue;
		}
		else if (current_char == '\"' && !escaped)
		{
			in_string = !in_string;
		}
		else if (!in_string && strncmp(string, start_token, start_token_len) == 0)
		{
			for (i = 0; i < start_token_len; i++)
				string[i] = ' ';

			string 	= string + start_token_len;
			ptr 	= strstr(string, end_token);

			if (!ptr)
				return;

			for (i = 0; i < (ptr - string) + end_token_len; i++)
				string[i] = ' ';

			string = ptr + end_token_len - 1;
		}

		escaped = 0;
		string++;
	}
}
/**********************************************************************************************************************/
static void skip_quotes(const char **str)
{
	skip_char(str);

	while (**str != '\"')
	{
		if (**str == '\0')
			return;

		if (**str == '\\')
		{
			skip_char(str);

			if (**str == '\0')
				return;
		}

		skip_char(str);
	}

	skip_char(str);
}
/**********************************************************************************************************************/
static int BrbStrIsUTF(const unsigned char *str)
{
	return isxdigit(str[0]) && isxdigit(str[1]) && isxdigit(str[2]) && isxdigit(str[3]);
}
/**********************************************************************************************************************/
static int BrbStrIsDecimal(const char *str, long length)
{
	if (!str)
		return 0;

	if (length > 1 && str[0] == '0' && str[1] != '.')
		return 0;

	if (length > 2 && !strncmp(str, "-0", 2) && str[2] != '.')
		return 0;

	while (length--)
		if (strchr("xX", str[length]))
			return 0;

	return 1;
}
/**********************************************************************************************************************/
