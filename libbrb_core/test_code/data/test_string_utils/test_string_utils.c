/*
 * test_string_utils.c
 *
 *  Created on: 2018-01-19
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *
 *
 * Copyright (c) 2018 BrByte Software (Oliveira Alves & Amorim LTDA)
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

/**************************************************************************************************************************/
static int BrbStrFindSubStr2(char *buffer_str, int buffer_sz, char *substring_str, int substring_sz)
{
	int idx;
	int token_idx;

	/* check headers */
	for (idx = 0, token_idx = 0; (idx < buffer_sz) && (token_idx < substring_sz); idx++)
	{
		token_idx = 0;

		/* skip until we find first char terminator*/
		if (buffer_str[idx] != substring_str[token_idx])
			continue;

		while( (token_idx < substring_sz) && (idx < buffer_sz) )
		{
			token_idx++;
			idx++;

			/* skip until we find first char terminator*/
			if (buffer_str[idx] != substring_str[token_idx])
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
int WebEngineBasePostParseFromHTTPRequestMultiPart(void)
{
//	HttpHeaderContentType *hdr_content_type 		= &http_req->http_hdr.hdr_content_type;
//	HttpHeaderContentTypeMultipart *ctype_multipart = &hdr_content_type->multipart;

	char *body_str;
	int body_sz;

	char *boundary_ptr;
	char *param_key_str;
	char *param_val_str;
	int param_key_sz;
	int param_val_sz;

	int boundary_sz;

	int param_offset;
	int boundary_offset;

//	printf("MULTIPART [%d] - BOUNDARY [%d] - [%s] - BODY [%p]\n", ctype_multipart->type, ctype_multipart->boundary_sz, ctype_multipart->boundary, http_req->http_body.body_data);

//	if (!http_req->http_body.body_data)
//		return 0;

//	if (HTTP_BODY_TYPE_MULTIPART != http_req->http_body.body_type)
//		return 0;

//	boundary_ptr	= (char *)&ctype_multipart->boundary;
	boundary_ptr	= "----WebKitFormBoundaryaileiWeBVNSIv4A2";
	boundary_sz		= strlen(boundary_ptr);

//	body_str		= MemBufferDeref(http_req->http_body.body_data);
//	body_sz			= MemBufferGetSize(http_req->http_body.body_data);

	body_str		= "------WebKitFormBoundaryaileiWeBVNSIv4A2\r\nContent-Disposition: form-data; name=\"action\"\r\n\r\ncreate\r\n------WebKitFormBoundaryaileiWeBVNSIv4A2\r\nContent-Disposition: form-data; name=\"ura_pk\"\r\n\r\n\r\n------WebKitFormBoundaryaileiWeBVNSIv4A2\r\nContent-Disposition: form-data; name=\"ura_active\"\r\n\r\n0\r\n------WebKitFormBoundaryaileiWeBVNSIv4A2\r\nContent-Disposition: form-data; name=\"ura_name\"\r\n\r\nasd\r\n------WebKitFormBoundaryaileiWeBVNSIv4A2\r\nContent-Disposition: form-data; name=\"file_name\"\r\n\r\nVoiprAppRedirecionamento.png\r\n------WebKitFormBoundaryaileiWeBVNSIv4A2\r\nContent-Disposition: form-data; name=\"file_content_type\"\r\n\r\nimage/png\r\n------WebKitFormBoundaryaileiWeBVNSIv4A2\r\nContent-Disposition: form-data; name=\"file_path\"\r\n\r\n/tmp/1/0000000001\r\n------WebKitFormBoundaryaileiWeBVNSIv4A2\r\nContent-Disposition: form-data; name=\"file_md5\"\r\n\r\n385adee729ab545b3e4a4cff554138db\r\n------WebKitFormBoundaryaileiWeBVNSIv4A2\r\nContent-Disposition: form-data; name=\"file_size\"\r\n\r\n28498\r\n------WebKitFormBoundaryaileiWeBVNSIv4A2--\r\n";
	body_sz			= strlen(body_str);

	/* Explode form_url_encoded variables using & separator char */
//	post_vars->raw_body_arr 	= StringArrayNew(BRBDATA_THREAD_UNSAFE, body_sz);

	boundary_offset = BrbStrFindSubStr(body_str, body_sz, boundary_ptr, boundary_sz);

	/* some error occur, skip parser */
	if (boundary_offset < 0)
		return 0;

	/* SKIP \r\n */
	boundary_offset = (boundary_offset + 2);

	/* adjust pointer */
	body_str		= (body_str + boundary_offset);
	body_sz			= (body_sz - boundary_offset);

	while ((body_sz > 0) && (body_sz > boundary_sz))
	{
		param_offset 	= BrbStrFindSubStr(body_str, body_sz, "; name=\"", 8);

		/* some error occur, skip parser */
		if (param_offset < 0)
			break;

		param_offset++;

		/* adjust pointer */
		body_str		= (body_str + param_offset);
		body_sz			= (body_sz - param_offset);

		if (body_sz <= 0)
			break;

		/* find until found ", finish of parameter name */
		param_offset 	= BrbStrFindSubStr(body_str, body_sz, "\"", 1);

		param_key_str	= body_str;
		param_key_sz	= param_offset;

		/* skip "\r\n\r\n */
		param_offset	= (param_offset + 5);

		/* adjust pointer */
		body_str		= (body_str + param_offset);
		body_sz			= (body_sz - param_offset);

		if (body_sz <= 0)
			break;

		boundary_offset = BrbStrFindSubStr(body_str, body_sz, boundary_ptr, boundary_sz);

		/* some error occur, skip parser */
		if (boundary_offset < 0)
			return 0;

		boundary_offset++;

		param_val_str	= body_str;
		param_val_sz	= (boundary_offset - boundary_sz - 4);

		/* adjust pointer */
		body_str		= (body_str + boundary_offset);
		body_sz			= (body_sz - boundary_offset);

		printf("PARAM (%.*s)\n", param_key_sz, param_key_str);
		printf("VALUE (%.*s)\n\n", param_val_sz, param_val_str);

	}

	return 1;
}
/************************************************************************************************************************/
int main(int argc, char **argv)
{
	int terminator_off;

//	WebEngineBasePostParseFromHTTPRequestMultiPart();

//	terminator_off = BrbStrFindSubStr2("AT+CSQ?\r\r\r\r\r\r\n+CSQ..19.99\r\n\r\nOK\r\nAT+CSQ\r\r\nCSQ1999", 45, "\r\n", 2);
//	printf("TERMINATOR - [%d]\n\n", terminator_off);
	terminator_off = BrbStrFindSubStr("-ERR gsmopen_dump Command not found!\n", 37, "\n\n", 2);
	printf("TERMINATOR - [%d]\n\n", terminator_off);
	printf("------------------------------\n");

//	terminator_off = BrbStrFindSubStr("ABCDE", 5, "A", 1);
//	printf("TERMINATOR - [%d]\n\n", terminator_off);
//	terminator_off = BrbStrFindSubStr2("ABCDE", 5, "A", 1);
//	printf("TERMINATOR2 - [%d]\n\n", terminator_off);
//	printf("------------------------------\n");

//	terminator_off = BrbStrFindSubStr("ABCDE", 5, "ABC", 3);
//	printf("TERMINATOR - [%d]\n\n", terminator_off);
//	terminator_off = BrbStrFindSubStr2("ABCDE", 5, "ABC", 3);
//	printf("TERMINATOR2 - [%d]\n\n", terminator_off);
//	printf("------------------------------\n");

//	terminator_off = BrbStrFindSubStr("ABCDE", 5, "B", 1);
//	printf("TERMINATOR - [%d]\n\n", terminator_off);
//	terminator_off = BrbStrFindSubStr2("ABCDE", 5, "B", 1);
//	printf("TERMINATOR2 - [%d]\n\n", terminator_off);
//	printf("------------------------------\n");

//	terminator_off = BrbStrFindSubStr("AABCDE", 5, "AB", 2);
//	printf("TERMINATOR - [%d]\n\n", terminator_off);
//	terminator_off = BrbStrFindSubStr2("AABCDE", 5, "AB", 2);
//	printf("TERMINATOR2 - [%d]\n\n", terminator_off);
//	printf("------------------------------\n");
//
//	terminator_off = BrbStrFindSubStr("AAB", 2, "AB", 2);
//	printf("TERMINATOR - [%d]\n\n", terminator_off);
//	terminator_off = BrbStrFindSubStr2("AAB", 3, "AB", 2);
//	printf("TERMINATOR2 - [%d]\n\n", terminator_off);
//	printf("------------------------------\n");
//
//	terminator_off = BrbStrFindSubStr("------WebKitFormBoundarySRYHtAYVA1A0QdjV", strlen("------WebKitFormBoundarySRYHtAYVA1A0QdjV"),
//				"----WebKitFormBoundarySRYHtAYVA1A0QdjV", strlen("----WebKitFormBoundarySRYHtAYVA1A0QdjV"));
//	printf("TERMINATOR - [%d]\n\n", terminator_off);
//	terminator_off = BrbStrFindSubStr2("------WebKitFormBoundarySRYHtAYVA1A0QdjV", strlen("------WebKitFormBoundarySRYHtAYVA1A0QdjV"),
//			"----WebKitFormBoundarySRYHtAYVA1A0QdjV", strlen("----WebKitFormBoundarySRYHtAYVA1A0QdjV"));
//	printf("TERMINATOR - [%d]\n\n", terminator_off);
//
//
//	terminator_off = BrbStrFindSubStr("------WebKitFormBoundarySRYHtAYVA1A0QdjVaa", strlen("------WebKitFormBoundarySRYHtAYVA1A0QdjVaa"),
//				"----WebKitFormBoundarySRYHtAYVA1A0QdjV", strlen("----WebKitFormBoundarySRYHtAYVA1A0QdjV"));
//	printf("TERMINATOR - [%d]\n\n", terminator_off);
//	terminator_off = BrbStrFindSubStr2("------WebKitFormBoundarySRYHtAYVA1A0QdjVaa", strlen("------WebKitFormBoundarySRYHtAYVA1A0QdjVaa"),
//			"----WebKitFormBoundarySRYHtAYVA1A0QdjV", strlen("----WebKitFormBoundarySRYHtAYVA1A0QdjV"));
//	printf("TERMINATOR - [%d]\n\n", terminator_off);

	return 0;
}
/************************************************************************************************************************/
