/*
 * regex.c
 *
 *  Created on: 2014-12-30
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

#include "../include/libbrb_core.h"

/**************************************************************************************************************************/
int BrbRegExCompile(BrbRegEx *brb_regex, char *regex_str)
{
	int status;

	/* sanitize */
	if (!brb_regex)
		return 0;

	brb_regex->flags.compiled 	= 0;

	if (!regex_str)
		return 0;

	/* Compile the Regular Expression */
//	status 	= regcomp(&brb_regex->reg, regex_str, REG_EXTENDED|REG_NOSUB);
	status	= regcomp(&brb_regex->reg, regex_str, REG_EXTENDED|REG_ICASE);

	/* Check by errors on compile failure */
	if (status != 0) {

//		char error_message[MAX_ERROR_MSG];
//
//		/* Try to get error */
//		regerror(status, regex, error_message, MAX_ERROR_MSG);
//
//		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "[ERROR] compiling [%s]: [%s]\n", regex_str, error_message);

		return 0;
	}

	brb_regex->flags.compiled 	= 1;

	return 1;
}
/**************************************************************************************************************************/
int BrbRegExCompare(BrbRegEx *brb_regex, char *cmp_str)
{
	int status;

	/* sanitize */
	if (!brb_regex || !cmp_str)
		return 0;

	/* Need to compile? */
	if (!brb_regex->flags.compiled)
		return 0;

	/* Try to match the RE with submitted text */
	status 	= regexec(&brb_regex->reg, cmp_str, BRB_REGEX_MATCH, brb_regex->match, 0);

	/* Match problem, status MUST be 0 when it's OK */
	if (status != 0)
		return 0;

	/* Match OK */
	return 1;
}
/**************************************************************************************************************************/
int BrbRegExReplace(BrbRegEx *brb_regex, char *orig_str, char *sub_str, char *buf_str, int buf_sz)
{
	regmatch_t	*match_ptr;
	char	*str_ptr;

	char *buf_offset	= (buf_str + buf_sz);
	int orig_sz 		= strlen(orig_str);
	int i;

	for (str_ptr = buf_str; str_ptr < buf_offset; str_ptr++)
	{
		*str_ptr 	= *sub_str++;

		switch (*str_ptr)
		{
		case '$':
			{
				*str_ptr = *sub_str++;

				switch (*str_ptr)
				{
					case '0':  case '1':  case '2':  case '3':  case '4':
					case '5':  case '6':  case '7':  case '8': case '9':
					{

						match_ptr 	= &brb_regex->match[*str_ptr - '0'];

						if ((i = match_ptr->rm_so) >= 0)
						{
							if (match_ptr->rm_eo > orig_sz)
							{
								/* buggy GNU regexec!! */
								match_ptr->rm_eo 	= orig_sz;
							}

							while (i < match_ptr->rm_eo && str_ptr < buf_offset)
							{
								*str_ptr++ 	= orig_str[i++];
							}
						}

						str_ptr--;

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
	}

	/* destination buffer too small */
	return 0;

}
/**************************************************************************************************************************/
int BrbRegExClean(BrbRegEx *brb_regex)
{
	/* Free RegEx */
	regfree(&brb_regex->reg);

	/* Reset Flag */
	brb_regex->flags.compiled = 0;

	return 1;
}
/**************************************************************************************************************************/
