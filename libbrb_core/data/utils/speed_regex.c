/*
 * speed_regex.c
 *
 *  Created on: 2011-01-19
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

/* XXX TODO: Check if it begins and ends with a *, and honor it */

/**************************************************************************************************************************/
SpeedRegEx *SpeedRegExNew(SpeedRegexType type)
{
	SpeedRegEx *ret_speedreg;

	/* Alloc new structure */
	BRB_CALLOC(ret_speedreg, 1,sizeof(SpeedRegEx));

	/* Set type */
	ret_speedreg->spdreg_type = type;

	return ret_speedreg;

}
/**************************************************************************************************************************/
void SpeedRegExDestroy (SpeedRegEx *speedreg)
{
	int i;
	char *str_ptr;
	int str_sz;


	/* Sanity Check */
	if (!speedreg)
		return;

	/* Free internal string array */
	StringArrayDestroy(speedreg->match_elem_arr);

	/* Free capsule */
	BRB_FREE(speedreg);

	return;
}
/**************************************************************************************************************************/
SpeedRegEx *SpeedRegExCompile(char *speed_regex, int order_match, int type)
{
	SpeedRegEx *ret_speedreg;
	int first_elem_sz;
	int last_elem_sz;
	int last_elem_pos;
	int arr_elem_count;


	//printf ("SpeedRegExCompile - processing strregex: %s\n", speed_regex);

	/* Create a new speed regex */
	ret_speedreg = SpeedRegExNew(type);

	/* Initialize speedregex struct */
	ret_speedreg->order_match		= order_match;
	ret_speedreg->match_elem_arr	= StringArrayExplodeStr(speed_regex, "*", "\\", NULL);

	/* Get size of element 0. If it is 0, it means caller put a * on the begin of spdregx string. So we want
	 * to match against elements, regarding of what is in the beginning. If it is > 0, it means first element must
	 * be in the begin of matched string
	 */
	arr_elem_count = StringArrayGetElemCount(ret_speedreg->match_elem_arr);

	last_elem_pos = (arr_elem_count - 1);

	first_elem_sz = StringArrayGetDataSizeByPos(ret_speedreg->match_elem_arr, 0);
	last_elem_sz = StringArrayGetDataSizeByPos(ret_speedreg->match_elem_arr, last_elem_pos);

	if (last_elem_sz > 0)
	{
		//printf("wants finish\n");
		ret_speedreg->wants_finish_with = 1;
	}
	else
	{
		/* Remove first empty element */
		StringArrayDeleteByPos(ret_speedreg->match_elem_arr, last_elem_pos);

		//printf("dont wants finish\n");
		ret_speedreg->wants_finish_with = 0;
	}

	/* Yes, we want to match begin */
	if (first_elem_sz > 0)
	{
		//printf("wants begin\n");
		ret_speedreg->wants_begin_with = 1;
	}

	/* We dont care with the beginning */
	else
	{
		/* Remove first empty element */
		StringArrayDeleteByPos(ret_speedreg->match_elem_arr, 0);

		//printf("dont wants begin\n");
		ret_speedreg->wants_begin_with = 0;
	}


	return ret_speedreg;
}
/**************************************************************************************************************************/
int SpeedRegExExecute (SpeedRegEx *speedreg, char *data)
{
	char *regstr_ptr;
	int regstr_sz = 0;
	int i;
	int match_arr_idx = 0;
	int last_item_flag = 0;
	int matchtable_sz = 0;

	char *match_ptr;
	char *cur_data_off;

	/* Sanity check */
	if ( (!speedreg) || (!data) )
		return 0;

	/* Get array count */
	matchtable_sz = StringArrayGetElemCount(speedreg->match_elem_arr);

	/* Create stack-based array */
	char *match_pos[matchtable_sz + 1];

	/* Clean stack buffer */
	memset(match_pos, 0, (matchtable_sz * sizeof(char*)));

	/* Grab cur data offset */
	cur_data_off = data;

	/* Process the array of strings against data, and save ptrs in match_pos */
	STRINGARRAY_FOREACH(speedreg->match_elem_arr, regstr_ptr, regstr_sz)
	{
		/* Match pos begin with 1 */
		match_arr_idx = _count_;

		/* Are we the last item */
		last_item_flag = ((matchtable_sz - 1) == match_arr_idx);

		/* We are on the first element of a speedregex that wants begin, so check with strcmp */
		if ((speedreg->wants_begin_with) && !match_arr_idx)
		{
			//printf ("First element of wants begin - comparsing %s with %s with depth %d\n", regstr_ptr, data, regstr_sz);

			if (strncmp(regstr_ptr, data, regstr_sz))
			{
				//printf("Do not begin with, returning false\n");

				return 0;
			}
		}

		/* We are on the last element of a speedregex that wants finish, so check with strcmp */
		if ((speedreg->wants_finish_with) && last_item_flag)
		{
			char *offset;
			int data_sz = strlen(data);

			/* Get offset to begin of last data item */
			offset = (data + (data_sz - regstr_sz));

			//printf ("Last element of wants finish - comparsing %s with %s with depth %d\n", regstr_ptr, offset, regstr_sz);

			if (strncmp(regstr_ptr, offset, regstr_sz))
			{
				//printf("Do not finish with, returning false\n");

				return 0;

			}

		}

		//printf ("comparsing %s with %s\n", regstr_ptr, cur_data_off);

		/* Locate a ptr for this substring */
		match_ptr = strstr(cur_data_off, regstr_ptr);

		//printf ("match_ptr = %d\n", match_ptr);

		if (match_ptr)
			match_pos[match_arr_idx] = match_ptr;
		else
			match_pos[match_arr_idx] = NULL;

		if (match_pos[match_arr_idx])
		{

			//printf("FOUND. match_pos[%d]: %p\n", match_arr_idx, match_pos[match_arr_idx]);
		}

		else
		{
			//printf("NOT Found. match_pos[%d]: %p - RETURNING FALSE\n", match_arr_idx, match_pos[match_arr_idx]);

			return 0;
		}

		cur_data_off += regstr_sz;
	}


	switch (speedreg->order_match)
	{

	/* We care about element order */
	case SPDREGEX_ORDER_SENSE:

		/* We start this i with 1, because we will compare it with i-1 */
		for (i = 1; i < matchtable_sz; i++)
		{
			//printf("order match - comparing pos[%d] = %d - pos[%d] = %d\n", i, match_pos[i], (i-1), match_pos[i-1]);

			if ((match_pos[i] == 0) || (match_pos[i-1] == 0))
			{
				//printf("\tNULL-element in match_pos, returning 0\n");
				return 0;
			}
			if (match_pos[i] < match_pos[i-1])
			{
				//printf("\tUnordered match_pos, returning 0\n");
				return 0;
			}

		}

		//printf("\tALL OK. returning true\n");
		return 1;

		/* As long the elements appear, we are ok */
	case SPDREGEX_ORDER_INSENSE:

		for (i = 1; i < matchtable_sz; i++)
		{

			if ((match_pos[i] == 0) || (match_pos[i-1] == 0))
				return 0;
		}

		//printf("\treturning true\n");
		return 1;
	}

	//printf("\tReturning FLASE 0\n");

	return 0;

}
/**************************************************************************************************************************/
