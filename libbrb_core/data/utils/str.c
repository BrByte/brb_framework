/*
 * str.c
 *
 *  Created on: 2021-05-14
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

/**************************************************************************************************************************/
BrbStr BrbStrNew(const char *buf_str)
{
	BrbStr str = { buf_str, buf_str == NULL ? 0 : strlen(buf_str) };

	return str;
}
/**************************************************************************************************************************/
BrbStr BrbStrNewN(const char *buf_str, size_t buf_sz)
{
	BrbStr str = { buf_str, buf_sz };
	return str;
}
/**************************************************************************************************************************/
BrbStr BrbStrdup(const BrbStr s)
{
	BrbStr r = BRB_STR_NULL;

	if (s.len > 0 && s.ptr != NULL)
	{
		char *sc = (char*) malloc(s.len + 1);
		if (sc != NULL)
		{
			memcpy(sc, s.ptr, s.len);
			sc[s.len] = '\0';
			r.ptr = sc;
			r.len = s.len;
		}
	}

	return r;
}
/**************************************************************************************************************************/
BrbStr BrbStrstrip(BrbStr s)
{
	while (s.len > 0 && isspace((int ) *s.ptr))
		s.ptr++, s.len--;

	while (s.len > 0 && isspace((int ) *(s.ptr + s.len - 1)))
		s.len--;

	return s;
}
/**************************************************************************************************************************/
int BrbStr_tolower(const char *buf_str)
{
	return tolower(*(const unsigned char* ) buf_str);
}
/**************************************************************************************************************************/
int BrbStr_ncasecmp(const char *s1, const char *s2, size_t len)
{
	int diff = 0;

	if (len > 0)
	{
		do
		{
			diff = BrbStr_tolower(s1++) - BrbStr_tolower(s2++);

		} while (diff == 0 && s1[-1] != '\0' && --len > 0);
	}

	return diff;
}
/**************************************************************************************************************************/
int BrbStr_casecmp(const char *s1, const char *s2)
{
	return BrbStr_ncasecmp(s1, s2, (size_t) ~0);
}
/**************************************************************************************************************************/
int BrbStr_vcmp(const BrbStr *s1, const char *s2)
{
	size_t n2 = strlen(s2), n1 = s1->len;

	int r = strncmp(s1->ptr, s2, (n1 < n2) ? n1 : n2);

	if (r == 0)
		return (int) (n1 - n2);

	return r;
}
/**************************************************************************************************************************/
int BrbStr_vcasecmp(const BrbStr *str1, const char *str2)
{
	size_t n2 = strlen(str2), n1 = str1->len;
	int r = BrbStr_ncasecmp(str1->ptr, str2, (n1 < n2) ? n1 : n2);

	if (r == 0)
		return (int) (n1 - n2);

	return r;
}
/**************************************************************************************************************************/
int BrbStr_cmp(const BrbStr str1, const BrbStr str2)
{
	size_t i = 0;

	while (i < str1.len && i < str2.len)
	{
		int c1 = str1.ptr[i];
		int c2 = str2.ptr[i];

		if (c1 < c2)
			return -1;

		if (c1 > c2)
			return 1;

		i++;
	}

	if (i < str1.len)
		return 1;

	if (i < str2.len)
		return -1;

	return 0;
}
/**************************************************************************************************************************/
const char *BrbStrstr(const BrbStr haystack, const BrbStr needle)
{
	size_t i;

	if (needle.len > haystack.len)
		return NULL ;

	for (i = 0; i <= haystack.len - needle.len; i++)
	{
		if (memcmp(haystack.ptr + i, needle.ptr, needle.len) == 0)
		{
			return haystack.ptr + i;
		}
	}
	return NULL ;
}
/**************************************************************************************************************************/
int BrbStrReplaceAllNonAlpha(char *buf_str, int buf_sz)
{
	unsigned long j = 0;
	unsigned long i = 0;
	char c;

	/* sanitize */
	if (!buf_str)
		return 0;

	while (i < buf_sz && (c = buf_str[i++]) != '\0')
	{
		if (isalnum(c) || c == '_')
		{
			buf_str[j++] = c;
		}
	}

	buf_str[j] 	= '\0';

	return 1;
}
/**************************************************************************************************************************/
char *BrbStrAddSlashes(const char *buf_str, int buf_sz)
{
	char *dst_buf;
	int i, j;

	if (!buf_str)
		return NULL;

	/* Allocate memory */
	dst_buf 	= calloc(1, ((buf_sz * 2) + 1));

	for (i = 0, j = 0; i < buf_sz; i++, j++)
	{
		/* Double slash */
		switch (buf_str[i])
		{
		case '\0': dst_buf[j++] = '\\';    break;
		case '\a': dst_buf[j++] = '\\';    break;
		case '\b': dst_buf[j++] = '\\';    break;
		case '\f': dst_buf[j++] = '\\';    break;
		case '\n': dst_buf[j++] = '\\';    break;
		case '\r': dst_buf[j++] = '\\';    break;
		case '\t': dst_buf[j++] = '\\';    break;
		case '\v': dst_buf[j++] = '\\';    break;
		case '\\': dst_buf[j++] = '\\';    break;
		//			case '\?': dst_buf[j++] = '\\';    break;
		case '\'': dst_buf[j++] = '\\';    break;
		case '\"': dst_buf[j++] = '\\';    break;

		}

		//    	if ('\\' == buf_str[i])
		//    		dst_buf[j++] = '\\';

		/* Copy byte */
		dst_buf[j] = buf_str[i];

		continue;
	}

	/* NULL terminate it */
	dst_buf[j] = '\0';

	return dst_buf;
}
/**************************************************************************************************************************/
int BrbStrlcpySlashed(char *out_ptr, const char *in_ptr, int out_sz)
{
	if (!in_ptr || in_ptr[0] == '\0')
	{
		out_ptr[0] 				= '\0';
		return 0;
	}

	/* Add Slashes to String */
	int in_sz 		= strlen(in_ptr);
	char *in_sld 	= BrbStrAddSlashes(in_ptr, in_sz);

	in_sz 	= strlcpy(out_ptr, in_sld ? in_sld : "", out_sz);

	/* Free buffer */
	if (in_sld)
		free(in_sld);

	return in_sz;
}
/**************************************************************************************************************************/
char *BrbStrAddJSONSlashes(char *buf_str, int buf_sz)
{
	char *dst_buf;
	int i, j;

	if (!buf_str)
		return NULL;

	/* Allocate memory */
	dst_buf 	= calloc(1, ((buf_sz * 2) + 1));

	for (i = 0, j = 0; i < buf_sz; i++, j++)
	{
		/* Double slash */
		switch (buf_str[i])
		{
		case '\0': dst_buf[j++] = '\\'; dst_buf[j] = '0';  break;
		case '\a': dst_buf[j++] = '\\'; dst_buf[j] = 'a';  break;
		case '\b': dst_buf[j++] = '\\'; dst_buf[j] = 'b';  break;
		case '\f': dst_buf[j++] = '\\'; dst_buf[j] = 'f';  break;
		case '\n': dst_buf[j++] = '\\'; dst_buf[j] = 'n';  break;
		case '\r': dst_buf[j++] = '\\'; dst_buf[j] = 'r';  break;
		case '\t': dst_buf[j++] = '\\'; dst_buf[j] = 't';  break;
		case '\v': dst_buf[j++] = '\\'; dst_buf[j] = 'v';  break;
		case '\\': dst_buf[j++] = '\\'; dst_buf[j] = '\\'; break;
//		case '\?': dst_buf[j++] = '\\'; dst_buf[j] = '?';  break;
//		case '\'': dst_buf[j++] = '\\'; dst_buf[j] = '\''; break;
		case '\"': dst_buf[j++] = '\\'; dst_buf[j] = '\"'; break;
		default: 						dst_buf[j] = buf_str[i]; break;

		}

		//    	if ('\\' == buf_str[i])
		//    		dst_buf[j++] = '\\';

		/* Copy byte */
//		dst_buf[j] = buf_str[i];

		continue;
	}

	/* NULL terminate it */
	dst_buf[j] = '\0';

	return dst_buf;
}
/**************************************************************************************************************************/
int BrbStrSkipQuotes(char *buf_str, int buf_sz)
{
	int j;
	int i;

	/* sanitize */
	if (!buf_str)
		return 0;

	for (i = 0, j = 0; i < buf_sz; i++)
	{
		if (buf_str[i] != '"')
			buf_str[j++] = buf_str[i];
	}

	buf_str[j] 	= '\0';

	return 1;
}
/**************************************************************************************************************************/
int BrbStrFindSubStr(char *buf_str, int buf_sz, char *substring_str, int substring_sz)
{
	int idx;
	int token_idx;

	/* check headers */
	for (idx = 0, token_idx = 0; (idx < buf_sz) && (token_idx < substring_sz); idx++)
	{
		token_idx = 0;

		/* skip until we find first char terminator*/
		if (buf_str[idx] != substring_str[token_idx])
			continue;

		while( (token_idx < substring_sz) && (idx < buf_sz) )
		{
			token_idx++;
			idx++;

			/* skip until we find first char terminator*/
			if (buf_str[idx] != substring_str[token_idx])
			{
				/* Found all substring ? */
				if (token_idx == substring_sz)
					return (idx - 1);

				idx 	= (idx - token_idx);

				break;
			}
		}

		/* Found all substring ? */
		if (token_idx == substring_sz)
			return (idx - 1);

		continue;
	}

	/* Can't Found Substring */
	return -1;
}
/**************************************************************************************************************************/
int BrbStrUrlDecode(char *buf_str)
{
	char hexnum[3];
	int i, j;			/* i is write, j is read */
	unsigned int x;


	for (i = j = 0; buf_str[j]; i++, j++)
	{

		buf_str[i] = buf_str[j];

		if (buf_str[i] != '%')
			continue;

		/* %% case */
		if (buf_str[j + 1] == '%')
		{
			j++;
			continue;
		}

		if (buf_str[j + 1] && buf_str[j + 2])
		{
			/* %00 case */
			if (buf_str[j + 1] == '0' && buf_str[j + 2] == '0')
			{
				j += 2;
				continue;
			}

			hexnum[0] = buf_str[j + 1];
			hexnum[1] = buf_str[j + 2];
			hexnum[2] = '\0';

			if (1 == sscanf(hexnum, "%x", &x))
			{
				buf_str[i] = (char) (0x0ff & x);
				j += 2;
			}

		}
	}

	buf_str[i] = '\0';

	return strlen(buf_str);
}
/**************************************************************************************************************************/
char *BrbStrSkipNoNumeric(const char *buf_str, int buf_sz)
{
	int idx;

	/* buffer negative Grab size */
	if (buf_sz < 0)
	{
		buf_sz 	= buf_str ? strlen(buf_str) : 0;
	}

	/* sanitize */
	if (buf_sz <= 0)
		return NULL;

	/* check headers */
	for (idx = 0; idx < buf_sz; idx++)
	{
		/* skip until we find char terminator or number */
		if ((buf_str[idx] != '\0') && ((buf_str[idx] < '0') || (buf_str[idx] > '9')))
			continue;

		return (buf_str + idx);
	}

	return NULL;
}
/**************************************************************************************************************************/
int BrbStrDateToStructTimeVal(char *date_str, struct tm *calc_time, struct timeval *calc_val)
{
	struct timeval cur_timeval;
	struct tm data_time;
	struct tm *cur_time;

	if (!date_str || !calc_time || !calc_val)
		return -1;

	char date_a_year_str[5] 		= {0};
	char date_a_month_str[3] 		= {0};
	char date_a_day_str[3] 			= {0};
	char date_a_hour_str[3] 		= {0};
	char date_a_minute_str[3] 		= {0};
	char date_a_second_str[3] 		= {0};
	int date_sz 					= strlen(date_str);

	/* Reset info */
	memset(calc_time, 0, sizeof(struct tm));
	memset(calc_val, 0, sizeof(struct timeval));

	/* 01234567890123456789012345678 */
	/* 2022-06-01T00:37:56.000-03:00 */
	/* 2023-04-19T08:34:39-03:00 */
	/* 2023-05-18 00:00:00.0 */
	/* DATE FORMAT ISO8601
	 * YYYY-MM-DD HH:II:SS */
	if ((date_sz >= 19) && date_str[4] == '-' && date_str[7] == '-' && (date_str[10] == ' ' || date_str[10] == 'T') && date_str[13] == ':' && date_str[16] == ':')
	{
		/* set values to date_a */
		strncpy((char*) &date_a_year_str, 	(char*) &date_str[0], 4);
		strncpy((char*) &date_a_month_str, 	(char*) &date_str[5], 2);
		strncpy((char*) &date_a_day_str, 	(char*) &date_str[8], 2);
		strncpy((char*) &date_a_hour_str, 	(char*) &date_str[11], 2);
		strncpy((char*) &date_a_minute_str, (char*) &date_str[14], 2);
		strncpy((char*) &date_a_second_str, (char*) &date_str[17], 2);

		if (date_str[19] == '.')
		{
			calc_val->tv_usec 		= atoi(date_str + 20);
		}
	}
	/* 0123456789012345678*/
	/* YYYY-MM-DD */
	else if (date_sz == 10 && date_str[4] == '-' && date_str[7] == '-')
	{
		/* set values to date_a */
		strncpy((char*) &date_a_year_str, 	(char*) &date_str[0], 4);
		strncpy((char*) &date_a_month_str, 	(char*) &date_str[5], 2);
		strncpy((char*) &date_a_day_str, 	(char*) &date_str[8], 2);
	}
	/* 0123456789012345678*/
	/* DD/MM/YYYY */
	else if (date_sz == 10 && date_str[2] == '/' && date_str[5] == '/')
	{
		/* set values to date_a */
		strncpy((char*) &date_a_year_str, 	(char*) &date_str[6], 4);
		strncpy((char*) &date_a_month_str, 	(char*) &date_str[3], 2);
		strncpy((char*) &date_a_day_str, 	(char*) &date_str[0], 2);
	}
	/* 0123456789012345678*/
	/* DD.MM.YYYY */
	else if (date_sz == 10 && date_str[2] == '.' && date_str[5] == '.')
	{
		/* set values to date_a */
		strncpy((char*) &date_a_year_str, 	(char*) &date_str[6], 4);
		strncpy((char*) &date_a_month_str, 	(char*) &date_str[3], 2);
		strncpy((char*) &date_a_day_str, 	(char*) &date_str[0], 2);
	}
	else
	{
		/* Invalid date */
		return -1;
	}

	gettimeofday(&cur_timeval, NULL);
	cur_time 				= localtime_r((time_t*) &cur_timeval.tv_sec, &data_time);

	/* Keep offsets info */
	date_a_year_str[4] 		= '\0';
	date_a_month_str[2] 	= '\0';
	date_a_day_str[2] 		= '\0';
	date_a_hour_str[2] 		= '\0';
	date_a_minute_str[2] 	= '\0';
	date_a_second_str[2] 	= '\0';

	/* Fill TIMEVAL */
	calc_time->tm_year 		= atoi((char*) &date_a_year_str) - 1900;
	calc_time->tm_mon 		= atoi((char*) &date_a_month_str) - 1;
	calc_time->tm_mday 		= atoi((char*) &date_a_day_str);
	calc_time->tm_hour 		= atoi((char*) &date_a_hour_str);
	calc_time->tm_min 		= atoi((char*) &date_a_minute_str);
	calc_time->tm_sec 		= atoi((char*) &date_a_second_str);
	calc_time->tm_isdst 	= cur_time->tm_isdst;

	calc_val->tv_sec 		= mktime(calc_time);

	return 0;
}
/**************************************************************************************************************************/
int BrbStrDateToTm(const char *date_str, struct tm *calc_time)
{
	struct timeval cur_timeval;
	struct tm *cur_time;

	char date_a_year_str[5] 		= {0};
	char date_a_month_str[3] 		= {0};
	char date_a_day_str[3] 			= {0};
	char date_a_hour_str[3] 		= {0};
	char date_a_minute_str[3] 		= {0};
	char date_a_second_str[3] 		= {0};
	int date_month 					= 0;
	int date_sz 					= date_str ? strlen(date_str) : -1;

	/* Reset info */
	memset(calc_time, 0, sizeof(struct tm));

	/* 2022-06-01T00:37:56.000-03:00 */
	/* 2023-04-19T08:34:39-03:00 */
	/* 2023-05-18 00:00:00.0 */
	/* 01234567890123456789012345678 */
	/* DATE FORMAT ISO8601
	 * YYYY-MM-DD HH:II:SS */
	if ((date_sz >= 19) && date_str[4] == '-' && date_str[7] == '-' && (date_str[10] == ' ' || date_str[10] == 'T') && date_str[13] == ':' && date_str[16] == ':')
	{
		/* set values to date_a */
		strncpy((char*) &date_a_year_str, 	(char*) &date_str[0], 4);
		strncpy((char*) &date_a_month_str, 	(char*) &date_str[5], 2);
		strncpy((char*) &date_a_day_str, 	(char*) &date_str[8], 2);
		strncpy((char*) &date_a_hour_str, 	(char*) &date_str[11], 2);
		strncpy((char*) &date_a_minute_str, (char*) &date_str[14], 2);
		strncpy((char*) &date_a_second_str, (char*) &date_str[17], 2);
	}
	/* 0123456789012345678*/
	/* YYYY-MM-DD */
	else if (date_sz == 10 && date_str[4] == '-' && date_str[7] == '-')
	{
		/* set values to date_a */
		strncpy((char*) &date_a_year_str, 	(char*) &date_str[0], 4);
		strncpy((char*) &date_a_month_str, 	(char*) &date_str[5], 2);
		strncpy((char*) &date_a_day_str, 	(char*) &date_str[8], 2);
	}
	/* 0123456789012345678*/
	/* DD/MM/YYYY */
	else if (date_sz == 10 && date_str[2] == '/' && date_str[5] == '/')
	{
		/* set values to date_a */
		strncpy((char*) &date_a_year_str, 	(char*) &date_str[6], 4);
		strncpy((char*) &date_a_month_str, 	(char*) &date_str[3], 2);
		strncpy((char*) &date_a_day_str, 	(char*) &date_str[0], 2);
	}
	/* 0123456789012345678*/
	/* DD.MM.YYYY */
	else if (date_sz == 10 && date_str[2] == '.' && date_str[5] == '.')
	{
		/* set values to date_a */
		strncpy((char*) &date_a_year_str, 	(char*) &date_str[6], 4);
		strncpy((char*) &date_a_month_str, 	(char*) &date_str[3], 2);
		strncpy((char*) &date_a_day_str, 	(char*) &date_str[0], 2);
	}
	/* 0123456789012345678*/
	/* DD MMM YYYY HH:II */
	else if (date_sz >= 17 && date_str[2] == ' ' && date_str[6] == ' ' && date_str[11] == ' ' && date_str[14] == ':')
	{
		/* set values to date_a */
		strncpy((char*) &date_a_year_str, 	(char*) &date_str[7], 4);
		strncpy((char*) &date_a_day_str, 	(char*) &date_str[0], 2);

		strncpy((char*) &date_a_hour_str, 	(char*) &date_str[12], 2);
		strncpy((char*) &date_a_minute_str, (char*) &date_str[15], 2);

		date_month 			= BrbStrDateMonthToNumber(&date_str[3]);
	}
	/* 0123456789012345678*/
	/* D MMM YYYY HH:II */
	else if (date_sz >= 16 && date_str[1] == ' ' && date_str[5] == ' ' && date_str[10] == ' ' && date_str[13] == ':')
	{
		/* set values to date_a */
		strncpy((char*) &date_a_year_str, 	(char*) &date_str[6], 4);
		strncpy((char*) &date_a_day_str, 	(char*) &date_str[0], 1);

		strncpy((char*) &date_a_hour_str, 	(char*) &date_str[11], 2);
		strncpy((char*) &date_a_minute_str, (char*) &date_str[14], 2);

		date_month 			= BrbStrDateMonthToNumber(&date_str[2]);
	}
	else
	{
		/* Invalid date */
		return -1;
	}

	gettimeofday(&cur_timeval, NULL);
	cur_time = localtime((time_t*) &cur_timeval.tv_sec);

	/* Keep offsets info */
	date_a_year_str[4] 		= '\0';
	date_a_month_str[2] 	= '\0';
	date_a_day_str[2] 		= '\0';
	date_a_hour_str[2] 		= '\0';
	date_a_minute_str[2] 	= '\0';
	date_a_second_str[2] 	= '\0';

	/* Fill TIMEVAL */
	calc_time->tm_year 		= atoi((char*) &date_a_year_str) - 1900;
	calc_time->tm_mon 		= (date_month > 0 ? date_month : atoi((char*) &date_a_month_str)) - 1;
	calc_time->tm_mday 		= atoi((char*) &date_a_day_str);
	calc_time->tm_hour 		= atoi((char*) &date_a_hour_str);
	calc_time->tm_min 		= atoi((char*) &date_a_minute_str);
	calc_time->tm_sec 		= atoi((char*) &date_a_second_str);
	calc_time->tm_isdst 	= cur_time->tm_isdst;

	return 0;
}
/**************************************************************************************************************************/
int BrbStrDateMonthToNumber(const char *date_str)
{
	if (!date_str)
		return 1;

	if (strncmp(date_str, "Jan", 3) == 0)
		return 1;

	if (strncmp(date_str, "Feb", 3) == 0)
		return 2;

	if (strncmp(date_str, "Mar", 3) == 0)
		return 3;

	if (strncmp(date_str, "Apr", 3) == 0)
		return 4;

	if (strncmp(date_str, "May", 3) == 0)
		return 5;

	if (strncmp(date_str, "Jun", 3) == 0)
		return 6;

	if (strncmp(date_str, "Jul", 3) == 0)
		return 7;

	if (strncmp(date_str, "Aug", 3) == 0)
		return 8;

	if (strncmp(date_str, "Sep", 3) == 0)
		return 9;

	if (strncmp(date_str, "Oct", 3) == 0)
		return 10;

	if (strncmp(date_str, "Nov", 3) == 0)
		return 11;

	if (strncmp(date_str, "Dec", 3) == 0)
		return 12;

	return 1;
}
/**************************************************************************************************************************/
int BrbTimeTmCalc(struct tm *in_time, struct tm *out_time, int diff_sec)
{
	struct tm *new_time;

	 // Convert in_time structure to a time_t value
    time_t t 	= mktime(in_time);

    // Add the difference in minutes to the time_t value
    t 			= t + (diff_sec);

    // Convert the modified value to a new tm structure
    new_time 	= localtime(&t);

    /* Copy new info */
    memcpy(out_time, new_time, sizeof(struct tm));

    return 0;
}
/**********************************************************************************************************************/
unsigned long long BrbStrToUnit(char* str)
{
	char *endptr 	= NULL;
	double num 		= strtod(str, &endptr);

	if (endptr == str)
		return -1;

	if (!endptr)
		return num;

	while (isspace(*endptr)) {
		endptr++;
	}

	double unit = 1;
	switch (*endptr)
	{
	case 'B': case 'b':
		break;
	case 'K': case 'k':
		unit *= pow(1024, 1);
		break;
	case 'M': case 'm':
		unit *= pow(1024, 2);
		break;
	case 'G': case 'g':
		unit *= pow(1024, 3);
		break;
	case 'T': case 't':
		unit *= pow(1024, 4);
		break;
	case 'P': case 'p':
		unit *= pow(1024, 5);
		break;
	default:
		return 0;
	}

	return (unsigned long long)(num * unit);
}
/**************************************************************************************************************************/

