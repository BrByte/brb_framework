/*
 * test_daemon.c
 *
 *  Created on: 2014-09-13
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
#include <libbrb_core.h>

EvKQBase *glob_ev_base;
EvKQBaseLogBase *glob_log_base;
int glob_timerid;

static EvBaseKQCBH mainTimerEvent;

///**************************************************************************************************************************/
//static int CommEvDHCPClientSocketConfig(CommEvDHCPClient *ev_dhcpclient)
//{
//	struct ifreq interface;
//	int op_status;
//
//	/* Set it to non_blocking and save it into newly allocated client */
//	op_status 		= EvKQBaseSocketSetNonBlock(ev_dhcpclient->kq_base, ev_dhcpclient->socket_fd);
//
//	/* Failed setting flag */
//	if (op_status < 0)
//	{
//		KQBASE_LOG_PRINTF(ev_dhcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed setting REUSE_ADDR on FD [%d] - ERRNO [%d / %s]\n", ev_dhcpclient->socket_fd, errno, strerror(errno));
//		close(ev_dhcpclient->socket_fd);
//		return -1;
//	}
//
//	/* Set socket to REUSE_ADDR flag */
//	op_status 		= EvKQBaseSocketSetReuseAddr(ev_dhcpclient->kq_base, ev_dhcpclient->socket_fd);
//
//	/* Failed setting flag */
//	if (op_status < 0)
//	{
//		KQBASE_LOG_PRINTF(ev_dhcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed setting REUSE_ADDR on FD [%d] - ERRNO [%d / %s]\n", ev_dhcpclient->socket_fd, errno, strerror(errno));
//		close(ev_dhcpclient->socket_fd);
//		return -2;
//	}
//
//	/* Set socket to REUSE_ADDR flag */
//	op_status 		= EvKQBaseSocketSetReusePort(ev_dhcpclient->kq_base, ev_dhcpclient->socket_fd);
//
//	/* Failed setting flag */
//	if (op_status < 0)
//	{
//		KQBASE_LOG_PRINTF(ev_dhcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed setting REUSE_PORT on FD [%d] - ERRNO [%d / %s]\n", ev_dhcpclient->socket_fd, errno, strerror(errno));
//		close(ev_dhcpclient->socket_fd);
//		return -3;
//	}
//
//	/* Set socket to REUSE_ADDR flag */
//	op_status 		= EvKQBaseSocketSetBroadcast(ev_dhcpclient->kq_base, ev_dhcpclient->socket_fd);
//
//	/* Failed setting flag */
//	if (op_status < 0)
//	{
//		KQBASE_LOG_PRINTF(ev_dhcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed setting REUSE_PORT on FD [%d] - ERRNO [%d / %s]\n", ev_dhcpclient->socket_fd, errno, strerror(errno));
//		close(ev_dhcpclient->socket_fd);
//		return -4;
//	}
//
//	/* Set socket to REUSE_ADDR flag */
//	op_status 		= CommEvDHCPClientSocketBind(ev_dhcpclient->kq_base, ev_dhcpclient->socket_fd, 0, NULL, DHCP_CLIENT_PORT);
//
//	/* Failed setting flag */
//	if (op_status < 0)
//	{
//		KQBASE_LOG_PRINTF(ev_dhcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed setting REUSE_PORT on FD [%d] - ERRNO [%d / %s]\n", ev_dhcpclient->socket_fd, errno, strerror(errno));
//		close(ev_dhcpclient->socket_fd);
//		return -5;
//	}
//
//	return 0;
//}
///**************************************************************************************************************************/
//static int CommEvDHCPClientSocketBind(EvKQBase *kq_base, int fd, int v6, void *bind_addr, int port)
//{
//	struct sockaddr_storage addr_me;
//	struct ifreq interface;
//	int op_status;
//
//	/* Initialize receiving socket on the device chosen */
//	memset(&addr_me, 0, sizeof(struct sockaddr_storage));
//
////	/* bind socket to interface */
////#if defined(__linux__)
////
////	strncpy(interface.ifr_ifrn.ifrn_name,network_interface_name,IFNAMSIZ);
////	if(setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, (char *)&interface, sizeof(interface)) < 0)
////	{
////		printf("Error: Could not bind socket to interface %s.  Check your privileges...\n",network_interface_name);
////		exit(STATE_UNKNOWN);
////	}
////
////#	else
////		strncpy(interface.ifr_name,network_interface_name,IFNAMSIZ);
////#	endif
//
//	/* Set up the address we're going to bind to. */
//	if (v6)
//	{
//		addr_me.ss_family				= AF_INET6;
//#if defined(__linux__)
//		addr_me.ss_len					= sizeof(struct sockaddr_in6);
//#endif
//		satosin6(&addr_me)->sin6_port	= htons(port);
//		satosin6(&addr_me)->sin6_addr 	= in6addr_any;
//
//		/* Copy BINDIP if selected */
//		if (bind_addr)
//			memcpy(&satosin6(&addr_me)->sin6_addr, bind_addr, sizeof(struct in6_addr));
//
//		op_status 	= bind(fd, (struct sockaddr *)&addr_me, sizeof(struct sockaddr_in6));
//	}
//	else
//	{
//		addr_me.ss_family				= AF_INET;
//#if defined(__linux__)
//		addr_me.ss_len					= sizeof(struct sockaddr_in);
//#endif
//		satosin(&addr_me)->sin_port			= htons(port);
//		satosin(&addr_me)->sin_addr.s_addr	= INADDR_ANY;
//
//		/* Copy BINDIP if selected */
//		if (bind_addr)
//			memcpy(&((struct sockaddr_in *)&addr_me)->sin_addr, bind_addr, sizeof(struct in_addr));
//
//		op_status 	= bind(fd, (struct sockaddr *)&addr_me, sizeof(struct sockaddr_in));
//	}
//
//	/* Bind to UDP port */
//	if (-1 == op_status)
//	{
//		char ip_str[64] = {0};
//
//		if (v6)
//			inet_ntop(AF_INET6, &satosin6(&addr_me)->sin6_addr, (char *)&ip_str, INET6_ADDRSTRLEN);
//		else
//			inet_ntop(AF_INET, &satosin(&addr_me)->sin_addr, (char *)&ip_str, INET_ADDRSTRLEN);
//
//		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed binding on FD [%d] - ADDR [%d]-[%s] - ERRNO [%d / %s]\n",
//				fd, v6, (char *)&ip_str, errno, strerror(errno));
//		close(fd);
//		return -4;
//	}
//
//	return 0;
//}
/**************************************************************************************************************************/
int main(int argc, char **argv)
{
	EvKQBaseLogBaseConf log_conf;
	int i;

	printf("RUN 01\n");

	memset(&log_conf, 0, sizeof(EvKQBaseLogBaseConf));
	log_conf.flags.double_write				= 1;
	log_conf.flags.mem_keep_logs			= 1;
//	log_conf.flags.mem_only_logs			= 1;
//	log_conf.mem_limit.bytes_total			= 8092;
//	log_conf.mem_limit.lines_total			= 20;

	/* Create event base */
	glob_ev_base	= EvKQBaseNew(NULL);
	glob_log_base	= EvKQBaseLogBaseNew(glob_ev_base, &log_conf);

	glob_ev_base->log_base = glob_log_base;

	printf("RUN 03\n");

	EvKQBaseInterceptSignals(glob_ev_base);

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "TEST_DAEMON starting [%d]\n", 1);

	for (i = 0; i < 16; i++)
	{
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "LOG_LINE [%d]\n", i);
		continue;
	}

	glob_timerid 	= EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 1000, mainTimerEvent, NULL);

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "PID [%d] - Begin on TIMER_ID [%d]\n", getpid(), glob_timerid);

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, 100);
	exit(0);

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int mainTimerEvent(int timer_id, int unused, int thrd_id, void *cb_data, void *base_ptr)
{
	/* Reschedule TIMER */
	glob_timerid 		= EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 1000, mainTimerEvent, NULL);

	printf("TIMER %d\n", glob_timerid);

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "PID [%d] - Reschedule on TIMER_ID [%d]\n", getpid(), glob_timerid);

	return 1;
}
/**************************************************************************************************************************/


