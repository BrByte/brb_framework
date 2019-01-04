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
char *BrbHexToStrStatic(char *hex, int hex_sz)
{
	char dst_buf[(hex_sz * 2) + 1];
	int size;
	int i;
	char *ret_ptr = (char*)&dst_buf;

	memset(&dst_buf, 0, sizeof(dst_buf));
	size = BrbHexToStr(hex, hex_sz, (char*)&dst_buf, sizeof(dst_buf));

	return ret_ptr;
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
char *BrbStrAddSlashes(char *src_str, int src_sz)
{
	char *dst_buf;
	int i, j;

	if (!src_str)
		return NULL;

	/* Allocate memory */
	dst_buf 	= calloc(1, ((src_sz * 2) + 1));

	for (i = 0, j = 0; i < src_sz; i++, j++)
	{
		/* Double slash */
		switch (src_str[i])
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

		//    	if ('\\' == src_str[i])
		//    		dst_buf[j++] = '\\';

		/* Copy byte */
		dst_buf[j] = src_str[i];

		continue;
	}

	/* NULL terminate it */
	dst_buf[j] = '\0';

	return dst_buf;
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
int BrbIsValidIpCidr(char *ip_cidr_str)
{
	char *mask_ptr;
	int	mask_value;
	int af_type;

	/* sanitize */
	if (!ip_cidr_str)
		return 0;

	mask_ptr 			= strchr(ip_cidr_str, '/');

	if (!mask_ptr)
		return -3;

	/* convert / to finish address string  */
	mask_ptr[0] 		= '\0';
	mask_value 			= atoi(&mask_ptr[1]);

	/* Check address */
	af_type 			= BrbIsValidIp(ip_cidr_str);

	/* convert again to / */
	mask_ptr[0] 		= '/';

	if (af_type == AF_UNSPEC)
		return -1;

	/* Get mask width */
	if ((mask_value < 0) || (af_type == AF_INET && mask_value > 32) || (af_type == AF_INET6 && mask_value > 128))
	{
		return -2;
	}

	return 1;
}
/**************************************************************************************************************************/
int BrbIsValidIp(char *ip_addr_str)
{
	struct sockaddr_storage target_sockaddr;

	return BrbIsValidIpToSockAddr(ip_addr_str, &target_sockaddr);
}
/**************************************************************************************************************************/
int BrbIsValidIpToSockAddr(char *ip_addr_str, struct sockaddr_storage *target_sockaddr)
{
	/* sanitize */
	if (!ip_addr_str || !target_sockaddr)
	{
		target_sockaddr->ss_family 	= AF_UNSPEC;
//		target_sockaddr->ss_len 	= 0;
	}
	else if (inet_pton(AF_INET, ip_addr_str, &((struct sockaddr_in *)target_sockaddr)->sin_addr) == 1)
	{
		target_sockaddr->ss_family 	= AF_INET;
//		target_sockaddr->ss_len 	= sizeof(struct sockaddr_in);
	}
	else if (inet_pton(AF_INET6, ip_addr_str, &((struct sockaddr_in6 *)target_sockaddr)->sin6_addr) == 1)
	{
		target_sockaddr->ss_family 	= AF_INET6;
//		target_sockaddr->ss_len 	= sizeof(struct sockaddr_in6);
	}
	/* Unknown case */
	else
	{
		target_sockaddr->ss_family 	= AF_UNSPEC;
//		target_sockaddr->ss_len 	= 0;
	}

	return target_sockaddr->ss_family;
}
/**************************************************************************************************************************/
int BrbIpFamilyParse(const char *ip_str, BrbIpFamily *ip_family, unsigned char allow)
{
	/* Can't parse CIDR */
	if (strchr(ip_str, '/'))
		return 0;

	/* Check Ipv4 valid */
	if ((allow & IP_FAMILY_ALLOW_IPV4) && inet_pton(AF_INET, ip_str, &ip_family->u.ip4))
	{
		ip_family->family 	= AF_INET;
	}
	/* Check Ipv6 valid */
	else if ((allow & IP_FAMILY_ALLOW_IPV6) && inet_pton(AF_INET6, ip_str, &ip_family->u.ip6))
	{
		ip_family->family 	= AF_INET6;
	}
	else
	{
		return 0;
	}

	return 1;
}
/**************************************************************************************************************************/
int BrbNetworkSockNtop(char *ret_data_ptr, int ret_maxlen, const struct sockaddr *sa, size_t salen)
{
	char	portstr[7];
	static	char str[128];	/* Unix domain is largest */

	int cur_sz = 0;

	switch (sa->sa_family)
	{
#if defined(SOCK_MAXADDRLEN)
	case SOCK_MAXADDRLEN:
	{
		int	i = 0;
		u_long	mask;
		u_int	index = 1 << 31;
		unsigned short	new_mask = 0;

		mask = ntohl(((struct sockaddr_in *)sa)->sin_addr.s_addr);

		while (mask & index)
		{
			new_mask++;
			index >>= 1;
		}

		cur_sz += snprintf(ret_data_ptr, ret_maxlen, "/%hu", new_mask);

		return 1;
	}
#endif
	case AF_UNSPEC:
	case AF_INET:
	{
		struct	sockaddr_in *sin = (struct sockaddr_in *)sa;
		static char buf_ptr[INET6_ADDRSTRLEN + 1];
		char *inet_ptr;

		inet_ptr 		= inet_ntop(AF_INET, &sin->sin_addr, (char *)&buf_ptr, sizeof(buf_ptr));

		if (!inet_ptr)
			return 0;

		cur_sz 			+= snprintf(ret_data_ptr, ret_maxlen, "%s", (char *)&buf_ptr);

		/* If have port */
		if (ntohs(sin->sin_port) != 0)
			cur_sz 		+= snprintf(ret_data_ptr + cur_sz, ret_maxlen, ".%d", ntohs(sin->sin_port));

//		/* If default route, write just default on it */
//		if (!strcmp(ret_data_ptr, "0.0.0.0"))
//			cur_sz = snprintf(ret_data_ptr, ret_maxlen,  "default");

		return 1;
	}
	case AF_INET6:
	{
		struct	sockaddr_in *sin = (struct sockaddr_in *)sa;
		static char buf_ptr[INET6_ADDRSTRLEN + 1];
		char *inet_ptr;

		inet_ptr 		= inet_ntop(AF_INET6, &((struct sockaddr_in6*)sin)->sin6_addr, buf_ptr, sizeof(buf_ptr));

		if (!inet_ptr)
			return 0;

		cur_sz 			+= snprintf(ret_data_ptr, ret_maxlen, "%s", buf_ptr);

		/* If have port */
		if (ntohs(sin->sin_port) != 0)
			cur_sz 		+= snprintf(ret_data_ptr + cur_sz, ret_maxlen, ".%d", ntohs(sin->sin_port));

		return 1;
	}

#if defined(AF_LINK)
	case AF_LINK:
	{
		struct	sockaddr_dl *sdl = (struct sockaddr_dl *)sa;
		char *cp;

		if (sdl->sdl_nlen > 0)
		{
//			switch (sdl->sdl_type) {
//
//			case IFT_ETHER:
//			case IFT_L2VLAN:
//			case IFT_BRIDGE:
//				if (sdl->sdl_alen == ETHER_ADDR_LEN) {
//					cp = ether_ntoa((struct ether_addr *)
//						(sdl->sdl_data + sdl->sdl_nlen));
//					break;
//				}
//				/* FALLTHROUGH */
//			default:
//				cp = link_ntoa(sdl);
//				break;
//			}
//
//			strlcpy(ret_data_ptr, cp, (sdl->sdl_nlen + 1));

			strlcpy(ret_data_ptr, sdl->sdl_data, (sdl->sdl_nlen + 1));
			//printf("SysControl_NetworkSockNtop - AFF_LINK -> [%d]-[%s]-[%s]\n", sdl->sdl_nlen, sdl->sdl_data, ret_data_ptr);

		}
		else
			snprintf(ret_data_ptr, ret_maxlen, "link#%d", sdl->sdl_index);

		return 1;
	}
#endif
	default:
		snprintf(ret_data_ptr, ret_maxlen, "unknown %d", sa->sa_family);
		return 0;
	}

	return 0;
}
/**************************************************************************************************************************/
int BrbNetworkSockMask(char *ret_data_ptr, int ret_maxlen, const struct sockaddr *sa)
{
	int cur_sz = 0;

	switch (sa->sa_family)
	{
	case AF_UNSPEC:
	case AF_INET:
	{
		int	i = 0;
		unsigned long	mask;
		unsigned int	index = 1 << 31;
		unsigned short	new_mask = 0;

		mask = ntohl(((struct sockaddr_in *)sa)->sin_addr.s_addr);

		while (mask & index)
		{
			new_mask++;
			index >>= 1;
		}

		cur_sz 	+= snprintf(ret_data_ptr, ret_maxlen, "/%hu", new_mask);

		return 1;
	}
	case AF_INET6:
	{
		struct in6_addr *mask = &((struct sockaddr_in6*)sa)->sin6_addr;
		unsigned char *p = (unsigned char *)mask;
		unsigned char *lim;
		int masklen, illegal = 0, flag = 0;

		for (masklen = 0, lim = p + 16; p < lim; p++) {
			switch (*p) {
			 case 0xff:
				 masklen += 8;
				 break;
			 case 0xfe:
				 masklen += 7;
				 break;
			 case 0xfc:
				 masklen += 6;
				 break;
			 case 0xf8:
				 masklen += 5;
				 break;
			 case 0xf0:
				 masklen += 4;
				 break;
			 case 0xe0:
				 masklen += 3;
				 break;
			 case 0xc0:
				 masklen += 2;
				 break;
			 case 0x80:
				 masklen += 1;
				 break;
			 case 0x00:
				 break;
			 default:
				 illegal ++;
				 break;
			}

			if (illegal)
				break;
		}

//		if (illegal)
//		{
//			cur_sz 	+= snprintf(ret_data_ptr, ret_maxlen, "/%d -%d", masklen, illegal);
//
//			return 0;
//		}

		cur_sz 	+= snprintf(ret_data_ptr, ret_maxlen, "/%d", masklen);

		return 1;
	}

	default:

		strlcpy(ret_data_ptr, "", 1);

		return 0;
	}

	return 0;
}
/**************************************************************************************************************************/
int BrbIsValidIpV4(char *ip_addr_str, struct in_addr *ip4)
{
	/* sanitize */
	if (!ip_addr_str)
		return 0;

	if (inet_pton(AF_INET, ip_addr_str, ip4))
	{
		return 1;
	}

	return 0;
}
/**************************************************************************************************************************/
int BrbIsNumeric(char *str)
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
int BrbIsHex(char *str)
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
int BrbIsDecimal(char *str)
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
int BrbStrFindSubStr(char *buffer_str, int buffer_sz, char *substring_str, int substring_sz)
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
int BrbStrUrlDecode(char *str)
{
	char hexnum[3];
	int i, j;			/* i is write, j is read */
	unsigned int x;


	for (i = j = 0; str[j]; i++, j++)
	{

		str[i] = str[j];

		if (str[i] != '%')
			continue;

		/* %% case */
		if (str[j + 1] == '%')
		{
			j++;
			continue;
		}

		if (str[j + 1] && str[j + 2])
		{
			/* %00 case */
			if (str[j + 1] == '0' && str[j + 2] == '0')
			{
				j += 2;
				continue;
			}

			hexnum[0] = str[j + 1];
			hexnum[1] = str[j + 2];
			hexnum[2] = '\0';

			if (1 == sscanf(hexnum, "%x", &x))
			{
				str[i] = (char) (0x0ff & x);
				j += 2;
			}

		}
	}

	str[i] = '\0';

	return strlen(str);
}
/**************************************************************************************************************************/
char *BrbStrSkipNoNumeric(char *buffer_str, int buffer_sz)
{
	int idx;

	/* buffer negative Grab size */
	if (buffer_sz < 0)
	{
		buffer_sz 	= buffer_str ? strlen(buffer_str) : 0;
	}

	/* sanitize */
	if (buffer_sz <= 0)
		return NULL;

	/* check headers */
	for (idx = 0; idx < buffer_sz; idx++)
	{
		/* skip until we find char terminator or number */
		if ((buffer_str[idx] != '\0') && ((buffer_str[idx] < '0') || (buffer_str[idx] > '9')))
			continue;

		return (buffer_str + idx);
	}

	return NULL;
}
/**************************************************************************************************************************/
long BrbDateToTimestamp(char *date_str)
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

	/* Date too smal, bail out */
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
unsigned char *BrbMacStrToOctedDup(char *mac_str)
{
	struct ether_addr *ether_addr;
	unsigned char mac[6];
	int i;

	ether_addr 				= ether_aton(mac_str);

	if (!ether_addr)
		return NULL;
#if defined(linux)
	for(i=0; i < 6; i++)
		mac[i] = (unsigned char)ether_addr->ether_addr_octet[i];
#else
	for(i=0; i < 6; i++)
		mac[i] = (unsigned char)ether_addr->octet[i];
#endif

	return (unsigned char *)memcpy(calloc(1, sizeof(unsigned int) * 6), mac, 6);
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
	return ( strcur[i] != '0' ? 1 : 0);
}
/**************************************************************************************************************************/
