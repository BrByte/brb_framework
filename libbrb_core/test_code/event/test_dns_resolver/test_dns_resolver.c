/*
 * test_dns_resolver.c
 *
 *  Created on: 2014-01-29
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

#include <libbrb_data.h>
#include <libbrb_ev_kq.h>

static CommEvDNSResolverCBH DNSResolverCB;

EvKQBase *glob_ev_base;
EvDNSResolverBase *glob_ev_dns;

/**************************************************************************************************************************/
static void DNSResolverCB(void *ev_dns_ptr, void *req_cb_data, void *a_reply_ptr, int code)
{
	int i;
	DNSAReply *a_reply = a_reply_ptr;
	static int count = 0;

	CommEvDNSGetHostByName(glob_ev_dns, "lic-elb.brbyte.com", DNSResolverCB, NULL);

	if (count++ > 64)
		exit(0);

	if (a_reply->ip_count > 0)
	{
		printf("DNSResolverCB - Host IP resolved - CODE [%d] - [%d] ADDRESS\n", code, a_reply->ip_count);

		for (i = 0; i < a_reply->ip_count; i++)
		{
			printf("DNSResolverCB - Index [%d] - IP_ADDR [%s] - TTL [%d]\n", i, inet_ntoa(a_reply->ip_arr[i].addr), a_reply->ip_arr[i].ttl);
		}

		return;
	}
	else
	{
		printf("DNSResolverCB - Failed to resolv with CODE [%d] - COUNT [%d]\n", code, a_reply->ip_count);
	}

	return;
}

/**************************************************************************************************************************/
int main(int argc, char **argv)
{
	EvDNSResolverConf dns_conf;
	int kq_retcode;

	glob_ev_base	= EvKQBaseNew(NULL);
	memset(&dns_conf, 0, sizeof(EvDNSResolverConf));

	if (argc < 2)
	{
		printf("Usage - %s HOSTNAME DNS_IP\n", argv[0]);
		exit(0);
	}

	/* Fill up DNS configuration */
	dns_conf.dns_ip_str			= argv[2] ? argv[2] : "8.8.8.8";
	dns_conf.dns_port			= 53;
	dns_conf.lookup_timeout_ms	= 500;
	dns_conf.retry_timeout_ms	= 50;
	dns_conf.retry_count		= 10;

	glob_ev_dns		= CommEvDNSResolverBaseNew(glob_ev_base, &dns_conf);

	/* Schedule new DNS query */
	CommEvDNSGetHostByName(glob_ev_dns, "lic_elb.brbyte.com", DNSResolverCB, NULL);



	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, 100);

	exit(0);
}
/**************************************************************************************************************************/
