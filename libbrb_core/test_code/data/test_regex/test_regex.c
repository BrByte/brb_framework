/*
 * test_regex.c
 *
 *  Created on: 2015-07-01
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *
 *
 * Copyright (c) 2015 BrByte Software (Oliveira Alves & Amorim LTDA)
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

#include <libbrb_core.h>

#define FILENAME_MAX 1024

static int regex_test(BrbRegEx *brb_regex, char *regex_test_str);
//static int BrbRegExReplace(BrbRegEx *brb_regex, char *orig_str, char *sub_str, char *buf_str, int buf_sz);

/****************************************************************************************************/
static int regex_test(BrbRegEx *brb_regex, char *regex_test_str)
{
	int regex_match;

	regex_match	= BrbRegExCompare(brb_regex, regex_test_str);

	printf("REGEX - MATCH [%d] - [%s]\n", regex_match, regex_test_str);

	return regex_match;
}
/****************************************************************************************************/
static int replace(
    const char* orig,			/* original string */
    const char* subst,			/* substitute containing \n's */
    regmatch_t match[],			/* substrings from regexec() */
    char *buf, size_t bufsiz)		/* destination buffer */
{
    char* s;
    size_t length = strlen(orig);

    int i;
    for (i = 0, s = buf; s < buf + bufsiz; s++, i++)
    {
    	*s = *subst++;

    	printf("I = %d - [%c]\n", i, *s);

		switch (*s)
		{
		case '$':

			*s = *subst++;

			printf("I - %d - [%c]\n", i, *s);

			switch (*s)
			{
				case '0':  case '1':  case '2':  case '3':  case '4':
				case '5':  case '6':  case '7':  case '8': case '9':
				{

					int i;
					regmatch_t* m = &match[*s - '0'];

					printf("HERE\n");

					if ((i = m->rm_so) >= 0)
					{
						if (m->rm_eo > length)
						{
							/* buggy GNU regexec!! */
							m->rm_eo = length;
						}

						while (i < m->rm_eo && s < buf+bufsiz)
						{
							*s++ = orig[i++];
						}
					}
					s--;
					break;
				}

				case '\0':
				/* shouldn't happen */
				return 1;
				}
			break;

			case '\0':
			{
				/* end of the substitution string */
				return 1;
			}

		}
    }

    /* destination buffer too small */
    return 0;
}
/**************************************************************************************************************************/
//static int BrbRegExReplace(BrbRegEx *brb_regex, char *orig_str, char *sub_str, char *buf_str, int buf_sz)
//{
//	regmatch_t	*match_ptr;
//	char	*str_ptr;
//
//	char *buf_offset	= (buf_str + buf_sz);
//	int orig_sz 	= strlen(orig_str);
//	int i;
//
//	for (str_ptr = buf_str; str_ptr < buf_offset; str_ptr++)
//	{
//		*str_ptr 	= *sub_str++;
//
//		switch (*str_ptr)
//		{
//		case '$':
//			{
//				*str_ptr = *sub_str++;
//
//				switch (*str_ptr)
//				{
//					case '0':  case '1':  case '2':  case '3':  case '4':
//					case '5':  case '6':  case '7':  case '8': case '9':
//					{
//
//						match_ptr 	= &brb_regex->match[*str_ptr - '0'];
//
//						if ((i = match_ptr->rm_so) >= 0)
//						{
//							if (match_ptr->rm_eo > orig_sz)
//							{
//								/* buggy GNU regexec!! */
//								match_ptr->rm_eo 	= orig_sz;
//							}
//
//							while (i < match_ptr->rm_eo && str_ptr < buf_offset)
//							{
//								*str_ptr++ 	= orig_str[i++];
//							}
//						}
//
//						str_ptr--;
//
//						break;
//					}
//
//					case '\0':
//
//					/* shouldn't happen */
//					return 1;
//				}
//				break;
//
//				case '\0':
//				{
//					/* end of the substitution string */
//					return 1;
//				}
//			}
//		}
//	}
//
//	/* destination buffer too small */
//	return 0;
//
//}
/****************************************************************************************************/
int main(void)
{
	BrbRegEx brb_regex;

	char buf_str[FILENAME_MAX];
	char *orig_str;
	int regex_match;
	int regex_replace;

//	brb_regex.flags.compiled	= BrbRegExCompile(&brb_regex, "([^;]*);|;([^;]*)$");
//	brb_regex.flags.compiled	= BrbRegExCompile(&brb_regex, "([^;]*);?(([^;]*)?)");
//	brb_regex.flags.compiled	= BrbRegExCompile(&brb_regex, "([^;]*);?([^;]*)");
	brb_regex.flags.compiled	= BrbRegExCompile(&brb_regex, "([^;]*);?([^;]*)?;?([^;]*)?;?([^;]*)?;?([^;]*)?;?([^;]*)?");


	if (brb_regex.flags.compiled)
	{

		orig_str		= "Luiz;31.00;5659832;lojas mourao";
//		orig_str		= "067;81234276";

		regex_match 	= regex_test(&brb_regex, orig_str);

		if (regex_match)
		{
			printf("MATCH [%s]\n", orig_str);

			regex_replace 	= BrbRegExReplace(&brb_regex, orig_str, "Ola $1, voce deve R$ $2, contrato $3, $4", buf_str, sizeof(buf_str));

			printf("REPLACE [%d] - [%s]\n", regex_replace, buf_str);

			regex_replace 	= BrbRegExReplace(&brb_regex, orig_str, "01541 $2", buf_str, sizeof(buf_str));

			printf("REPLACE [%d] - [%s]\n", regex_replace, buf_str);

		}
	}

//	regex_t re;
//
//	const char *orig = "06781234276";
//	const char *substring = "$1 $2 \\1 \\2 \\3";
//
//	int error;
//
//	/* "N_matches" is the maximum number of matches allowed. */
//	int n_matches = 10;
//
//	/* "M" contains the matches found. */
//	regmatch_t m[n_matches];
//
//	error 	= regcomp(&re, "(067)........", REG_EXTENDED|REG_ICASE);
//
//	if (error)
//	{
//		printf("NO COMPILE");
//		goto err;
//	}
//
//
//	error = regexec (&re, orig, n_matches, m, 0);
//
//	if (error)
//	{
//		printf("NO MATCH");
//		goto err;
//	}
//
//
//	replace(orig, substring, m, buf, sizeof(buf));
//
//	printf("%s\n", buf);
//
//	regfree (&re);
//
//	return 0;
//
//	err:
//
//	printf("ERROR\n");
//
//	regfree (&re);
//
//	return 1;


//
//	char *regex_test_str;
//	int regex_compiled;
//	int regex_match;
//
//	brb_regex.flags.compiled	= BrbRegExCompile(&brb_regex, "^(0[1-9]{2})?([0-9]{8})$");
//
//	/* compiled? test */
//	if (regex_compiled)
//	{
//
////		match_regex(&brb_regex, "06781234276");
////		match_regex(&brb_regex, "81234276");
////		match_regex(&brb_regex, "06751234276");
////		match_regex(&brb_regex, "32314114");
////		match_regex(&brb_regex, "13");
//
////		regex_test(&brb_regex, "06781234276");
////		regex_test(&brb_regex, "81234276");
////		regex_test(&brb_regex, "06751234276");
////		regex_test(&brb_regex, "32314114");
////		regex_test(&brb_regex, "13");
//
//	}
//
//	memset(&brb_regex, 0, sizeof(BrbRegEx));
//
//	brb_regex.flags.compiled	= BrbRegExCompile(&brb_regex, "^1[0-9]{4}$");
//
//	/* compiled? test */
//	if (regex_compiled)
//	{
//
//		regex_test(&brb_regex, "987662");
//		regex_test(&brb_regex, "55332");
//		regex_test(&brb_regex, "77663");
//
//	}

	return 1;
}
/**************************************************************************************************************************/
