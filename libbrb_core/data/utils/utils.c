/*
 * utils.c
 *
 *  Created on: 2011-10-11
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2011 BrByte Software (Oliveira Alves & Amorim LTDA)
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

#define INT_VALUE(c) ((c) - '0')

/**************************************************************************************************************************/
int BrbHexToStr(char *hex, int hex_sz, char *dst_buf, int dst_buf_sz)
{
	int offset;
	int i;

	for ((offset = 0, i = 0); ((i < hex_sz) && (offset < dst_buf_sz)); i++)
	{
		offset += sprintf((dst_buf + offset), "%02X ", (unsigned char)hex[i]);
		continue;
	}

	return offset;
}
/**************************************************************************************************************************/
char *BrbHexToStrAlloc(char *hex, int hex_sz)
{
	int size = (hex_sz * 3);
	int i;
	char *ret_ptr = calloc(1, size + 3);
	size = BrbHexToStr(hex, hex_sz, ret_ptr, size);

	return ret_ptr;
}
/**************************************************************************************************************************/
int BrbIsValidCpf(char *cpf_str)
{
	/* sanitize */
	if (!cpf_str)
		return 0;

	int i, j, digit1 = 0, digit2 = 0;

	if (strlen(cpf_str) != 11 && !BrbIsNumeric(cpf_str))
		return 0;

	else if(!strcmp(cpf_str,"00000000000") || !strcmp(cpf_str,"11111111111") || !strcmp(cpf_str,"22222222222") ||
			!strcmp(cpf_str,"33333333333") || !strcmp(cpf_str,"44444444444") || !strcmp(cpf_str,"55555555555") ||
			!strcmp(cpf_str,"66666666666") || !strcmp(cpf_str,"77777777777") || !strcmp(cpf_str,"88888888888") ||
			!strcmp(cpf_str,"99999999999"))
		return 0;
	else
	{
		///digit 1---------------------------------------------------
		for(i = 0, j = 10; i < strlen(cpf_str)-2; i++, j--)
			digit1 += INT_VALUE(cpf_str[i]) * j;

		digit1 %= 11;

		digit1 = (digit1 < 2)? 0 : 11 - digit1;

		if(INT_VALUE(cpf_str[9]) != digit1)
		{
			return 0;
		}
		else
		///digit 2--------------------------------------------------
		{
			for(i = 0, j = 11; i < strlen(cpf_str)-1; i++, j--)
					digit2 += INT_VALUE(cpf_str[i]) * j;

			digit2 %= 11;

			digit2 = (digit2 < 2)? 0 : 11 - digit2;

			if(INT_VALUE(cpf_str[10]) != digit2)
				return 0;
		}
	}
	return 1;
}
/**************************************************************************************************************************/
int BrbIsValidCnpj(char *cnpj_str)
{
	/* sanitize */
	if (!cnpj_str)
		return 0;

	/* Validate size and content */
	if(strlen(cnpj_str) != 14 && !BrbIsNumeric(cnpj_str))
		return 0;

	int i, j, sum = 0, rest = 0;

	/* Common invalid CNPJ */
	if (!strcmp(cnpj_str,"00000000000000"))
		return 0;
	else
	{
		///digit 1---------------------------------------------------
		for (i = 0, j = 5, sum = 0; i < 12; i++)
		{
			sum += (INT_VALUE(cnpj_str[i]) * j);
			j = (j == 2) ? 9 : j - 1;
		}

		rest = sum % 11;

		if (INT_VALUE(cnpj_str[12]) != (rest < 2 ? 0 : 11 - rest))
			return 0;

		///digit 2--------------------------------------------------
		for (i = 0, j = 6, sum = 0; i < 13; i++)
		{
			sum += INT_VALUE(cnpj_str[i]) * j;
			j = (j == 2) ? 9 : j - 1;
		}

		rest = sum % 11;

		if (INT_VALUE(cnpj_str[13]) != (rest < 2 ? 0 : 11 - rest))
			return 0;
	}
	return 1;
}
/**************************************************************************************************************************/
int BrbIsNumeric(const char *str)
{
	int status 	= 0;

	if (!str)
		return 0;

	if (str[0] == '-')
		str++;

	while(*str)
	{
		if(!isdigit(*str))
			return 0;

		str++;
		status++;
	}

	return status;
}
/**************************************************************************************************************************/
int BrbIsHex(const char *str)
{
	int status 	= 0;

	if (!str)
		return 0;

	if (*str != '0')
		return 0;

	str++;
	status++;

	if (!str)
		return 0;

	if (*str != 'x' && *str != 'X')
		return 0;

	str++;
	status++;

	while(str && *str)
	{
		if(!isxdigit(*str))
			return 0;

		str++;
		status++;
	}

	return status;
}
/**************************************************************************************************************************/
int BrbIsDecimal(const char *str)
{
	long length;

	if (!str)
		return 0;

	length = strlen(str);

	if (length > 1 && str[0] == '0' && str[1] != '.')
		return 0;

	if (length > 2 && !strncmp(str, "-0", 2) && str[2] != '.')
		return 0;

	while (length--)
		if (strchr("xX", str[length]))
			return 0;

	return 1;
}
/**************************************************************************************************************************/
int BrbIsNumberList(const char *num_str, char sep)
{
    // Flag to check if a number is currently being parsed
    int parsing_number = 0;
    int i = 0;

    if (!num_str)
    	return -1;

    while (num_str[i])
    {
        // Check if it's a digit
        if (num_str[i] >= '0' && num_str[i] <= '9')
        {
            parsing_number = 1;
        }
        // Check if it's the separator
        else if (num_str[i] == sep)
        {
            // If no number was parsed before encountering a separator, return pointer to separator
            if (!parsing_number)
                return i+1;

            // Reset the parsing_number flag for the next number
            parsing_number = 0;
        }
        else
        {
            // If it's neither a digit nor a separator, return pointer to the violating character
            return i+1;
        }

        i++;
    }

    // If parsing_number is true at the end, it means the string ends with a number without a separator
    return (parsing_number ? 0 : i+1);
}
/**************************************************************************************************************************/
long BrbDateToTimestamp(const char *date_str)
{
	struct timeval cur_timeval;
	struct tm *cur_time;
	struct tm calc_time_a;

	char date_a_year_str[5];
	char date_a_month_str[3];
	char date_a_day_str[3];
	char date_a_hour_str[3];
	char date_a_minute_str[3];
	char date_a_second_str[3];
	long ret_val;

	/* Date too small, bail out */
	if (strlen(date_str) < 19)
		return 0;

	/* Clean up stack */
	memset(&calc_time_a, 0, sizeof(struct tm));

	gettimeofday(&cur_timeval, NULL);
	cur_time = localtime((time_t*) &cur_timeval.tv_sec);

	/* set values to date_a */
	strncpy((char*) &date_a_year_str, 	(char*) &date_str[0], 4);
	strncpy((char*) &date_a_month_str, 	(char*) &date_str[5], 2);
	strncpy((char*) &date_a_day_str, 	(char*) &date_str[8], 2);
	strncpy((char*) &date_a_hour_str, 	(char*) &date_str[11], 2);
	strncpy((char*) &date_a_minute_str, (char*) &date_str[14], 2);
	strncpy((char*) &date_a_second_str, (char*) &date_str[17], 2);

	date_a_year_str[4] 		= '\0';
	date_a_month_str[2] 	= '\0';
	date_a_day_str[2] 		= '\0';
	date_a_hour_str[2] 		= '\0';
	date_a_minute_str[2] 	= '\0';
	date_a_second_str[2] 	= '\0';

	/* Fill TIMEVAL */
	calc_time_a.tm_year 	= atoi((char*) &date_a_year_str) - 1900;
	calc_time_a.tm_mon 		= atoi((char*) &date_a_month_str) - 1;
	calc_time_a.tm_mday 	= atoi((char*) &date_a_day_str);
	calc_time_a.tm_hour 	= atoi((char*) &date_a_hour_str);
	calc_time_a.tm_min 		= atoi((char*) &date_a_minute_str);
	calc_time_a.tm_sec 		= atoi((char*) &date_a_second_str);
	calc_time_a.tm_isdst 	= cur_time->tm_isdst;

	/* Calculate TIME */
	ret_val = mktime((struct tm*) &calc_time_a);

	return ret_val;
}
/**************************************************************************************************************************/
unsigned int BrbSimpleHashStrFmt(unsigned int seed, const char *key, ...)
{
	char fmt_buf[MEMBUFFER_MAX_PRINTF];
	va_list args;
	int msg_len;

	char *buf_ptr		= (char*)&fmt_buf;
	int msg_malloc		= 0;
	int alloc_sz		= MEMBUFFER_MAX_PRINTF;
	unsigned int hash	= 0;

	/* Probe message size */
	va_start(args, key);
	msg_len = vsnprintf(NULL, 0, key, args);
	va_end(args);

	/* Too big to fit on local stack, use heap */
	if (msg_len > (MEMBUFFER_MAX_PRINTF - 16))
	{
		/* Set new alloc size to replace default */
		alloc_sz	= (msg_len + 16);
		buf_ptr		= malloc(alloc_sz);
		msg_malloc	= 1;
	}

	/* Initialize VA ARGs */
	va_start(args, key);

	/* Now actually print it and NULL terminate */
	msg_len = vsnprintf(buf_ptr, (alloc_sz - 1), key, args);
	buf_ptr[msg_len] = '\0';

	/* Finish with VA ARGs list */
	va_end(args);

	/* Calculate HASH */
	hash = BrbSimpleHashStr(buf_ptr, msg_len, seed);

	/* Used MALLOC< release it */
	if (msg_malloc)
		free(buf_ptr);

	return hash;
}
/**************************************************************************************************************************/
unsigned int BrbSimpleHashStr(const char *key, unsigned int len, unsigned int seed)
{
	static const unsigned int c1 = 0xcc9e2d51;
	static const unsigned int c2 = 0x1b873593;
	static const unsigned int r1 = 15;
	static const unsigned int r2 = 13;
	static const unsigned int m = 5;
	static const unsigned int n = 0xe6546b64;

	unsigned int hash = seed;

	const int nblocks = len / 4;
	const unsigned int *blocks = (const unsigned int *) key;
	int i;
	for (i = 0; i < nblocks; i++) {
		unsigned int k = blocks[i];
		k *= c1;
		k = (k << r1) | (k >> (32 - r1));
		k *= c2;

		hash ^= k;
		hash = ((hash << r2) | (hash >> (32 - r2))) * m + n;
	}

	const unsigned char *tail = (const unsigned char *) (key + nblocks * 4);
	unsigned int k1 = 0;

	switch (len & 3) {
	case 3:
		k1 ^= tail[2] << 16;
	case 2:
		k1 ^= tail[1] << 8;
	case 1:
		k1 ^= tail[0];

		k1 *= c1;
		k1 = (k1 << r1) | (k1 >> (32 - r1));
		k1 *= c2;
		hash ^= k1;
	}

	hash ^= len;
	hash ^= (hash >> 16);
	hash *= 0x85ebca6b;
	hash ^= (hash >> 13);
	hash *= 0xc2b2ae35;
	hash ^= (hash >> 16);

	return hash;
}
/**************************************************************************************************************************/
int BrbStrToLower(char *str_ptr)
{
	int str_len = strlen(str_ptr);
	int i;

	for (i = 0; i < str_len; i++)
		str_ptr[i] = towlower(str_ptr[i]);

	return i;
}
/**************************************************************************************************************************/
int BrbStrToUpper(char *str_ptr)
{
	int str_len = strlen(str_ptr);
	int i;

	for (i = 0; i < str_len; i++)
		str_ptr[i] = towupper(str_ptr[i]);

	return i;
}
/**************************************************************************************************************************/
int BrbStrCompare(char *strcur, char *strcmp)
{
	int i;
	int limit;

	/* Sanity check */
	if ((!strcur) || (!strcmp))
		return 0;

	for (i = 0; (('\0' != strcur[i]) && ('\0' != strcmp[i])); i++)
	{
		/* Char is the same, avoid it */
		if (strcur[i] == strcmp[i])
			continue;

		/* Found different char, avail and return from here */
		if (strcur[i] > strcmp[i])
			return 1;
		else
			return 0;

	}

	/* strings begin with the same characters, check who have more characters to ordenate  */
	return ( strcur[i] != '\0' ? 1 : 0);
}
/**************************************************************************************************************************/
int BrbStrFindSubStrReverse(char *buffer_str, int buffer_sz, char *substring_str, int substring_sz)
{
	int idx;
	int token_idx;
	int i;

	/* 0000000000001230000  */
	/*                   i  */
	/*                   t  */

	/* 0000000000001230000  */
	/*                   i  */
	/*                   t  */

	if (buffer_sz < substring_sz)
		return -1;

	/* Start searching the buffer */
	for (i = (buffer_sz - substring_sz); i >= 0; i--)
	{
		/* Found finish of token, compare full token versus buffer */
		if ((buffer_str[i] == substring_str[0]) && (!memcmp(&buffer_str[i], substring_str, substring_sz)) )
			return i + substring_sz;

		continue;
	}

	/* Can't Found Substring */
	return -1;
}
/**************************************************************************************************************************/
char *BrbStrGetKeyByValue(BrbKeyValue val_key[], int k_value)
{
	int i;

	/* Search for type on global array */
	for (i = 0; val_key[i].key_ptr != NULL; i++)
	{
		/* Size do not match, move on */
		if (k_value != val_key[i].value)
			continue;

		/* Found it, return ID */
		return val_key[i].key_ptr;
	}

	return NULL;
}
/**************************************************************************************************************************/
int BrbStrGetValueByKey(BrbKeyValue val_key[], char *key_ptr)
{
	int type_name_sz;
	int i;

	if (!key_ptr)
		return 0;

	type_name_sz 	= strlen(key_ptr);

	/* Search for type on global array */
	for (i = 0; val_key[i].key_ptr && (val_key[i].key_ptr[0] != '\0'); i++)
	{
		/* Size do not match, move on */
		if (type_name_sz != val_key[i].key_sz)
			continue;

		/* Found it, return ID */
		if (!strncmp(val_key[i].key_ptr, key_ptr, val_key[i].key_sz))
			return val_key[i].value;
	}

	return 0;
}
/**************************************************************************************************************************/
/* Time Utils */
/**************************************************************************************************************************/
unsigned int BrbTimeLt(const struct timeval *a, const struct timeval *b)
{
    return (a->tv_sec == b->tv_sec) ? (a->tv_usec < b->tv_usec) : (a->tv_sec < b->tv_sec);
}
/**************************************************************************************************************************/
void BrbTimeSub(const struct timeval *a, const struct timeval *b, struct timeval *result)
{
    result->tv_sec 		= a->tv_sec - b->tv_sec;
    result->tv_usec 	= a->tv_usec - b->tv_usec;

    if (result->tv_usec < 0)
    {
        --result->tv_sec;
        result->tv_usec 	+= 1000000;
    }
}
/**************************************************************************************************************************/
int BrbStrValidateDomain(const char *buf_ptr, int buf_len)
{
	/* Sanitize */
    if (buf_ptr == NULL || strlen(buf_ptr) == 0)
        return -1;

    if (buf_len < 0)
    	buf_len 	= strlen(buf_ptr);

    /* Empty */
    if (buf_len <= 0)
        return -1;

    int dotCount = 0;

    // Domain name should not start or end with a dot or '-'
    if (buf_ptr[0] == '.' || buf_ptr[buf_len - 1] == '.' || buf_ptr[0] == '-' || buf_ptr[buf_len - 1] == '-')
        return -1;

    // Iterate through each character to check validity
    for (int i = 0; i < buf_len; i++)
    {
        char current = buf_ptr[i];

        // Each character should be alphanumeric, dot, or '-'
        if (!(isalnum(current) || current == '.' || current == '-'))
            return -2;

        // Dot should not appear consecutively
        if (current == '.' && (i == 0 || i == buf_len - 1 || buf_ptr[i - 1] == '-' || buf_ptr[i + 1] == '-' || buf_ptr[i + 1] == '.'))
            return -3;

        // Hyphen should not be before or after a dot
        if (current == '-' && (i == 0 || i == buf_len - 1 || buf_ptr[i - 1] == '.' || buf_ptr[i + 1] == '.'))
            return -4;

        // Count the number of dots
        if (current == '.')
        {
            dotCount++;
        }
    }

    // Domain should contain at least one dot
    if (dotCount == 0)
        return -5;

    return 0;
}
/**************************************************************************************************************************/
