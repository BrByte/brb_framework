/*
 * test_mem_buf_writeoffset.c
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

#include <libbrb_data.h>

//#include <stdio.h>
//#include <stdlib.h>
//#include <string.h>
//#include <unistd.h>
//#include <sys/stat.h>
//#include <fcntl.h>

typedef enum {
	PARAM_ERROR = -1,
	PARAM_ALL_OK,
	PARAM_LAST_UNUSED
} STATUS_PARAM;
/**************************************************************************************************************************/
int check_param(int argc)
{
	if (argc < 2)
		return PARAM_ERROR;
	if (argc == 2)
		return PARAM_LAST_UNUSED;

	return PARAM_ALL_OK;
}
/**************************************************************************************************************************/
int main(int argc, char *argv[])
{
	unsigned char *inFile, *outFile;
	MemBuffer *mb_ptr;

	/* Param switch
	/* --------------------------------------------------------------- */
	int param = check_param(argc);
	switch (param)
	{
		case PARAM_ALL_OK:
			inFile = argv[1];
			outFile = argv[2];
		break;

		case PARAM_LAST_UNUSED:
			printf("WARNING: unused last param, file output is .out\n");
			inFile = argv[1];
			asprintf((char**)&outFile, "%s.out", argv[1]);
		break;

		case PARAM_ERROR:
			printf("usage %s infile [outfile]\n", argv[0]);
			exit(-1);
		break;

	}

	//INIT_LIBDATA_BRB_MEM(UNINITIALIZED_SLAB, MEMSLAB_MTSAFE);

	/* Read file */
	mb_ptr = MemBufferReadFromFile(inFile);

	/* Check if file is opened */
	if(!mb_ptr) {
		printf("ERROR: cannot open(%s)\n", inFile);
		exit(1);
	}
	static const char test[] = "bbbbbbbbbbbbbbbbbbbbbbbb";

	MemBufferOffsetWrite(mb_ptr, 10, (void*)test, 10);

	printf("Reading %s and writting file %s with (%lu) bytes.\n", inFile, outFile, MemBufferGetSize(mb_ptr));

	/* Write output file */
	MemBufferWriteToFile(mb_ptr, outFile);

	/* Free MemBuffer */
	MemBufferDestroy(mb_ptr);

}
/**************************************************************************************************************************/
