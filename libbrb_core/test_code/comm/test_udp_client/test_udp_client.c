/*
 * test_udp_client.c
 *
 *  Created on: 2014-11-27
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
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE./
 */

#include <libbrb_core.h>

/************************************************************/
typedef struct _CommEvUDPClientConf
{
	EvKQBaseLogSub log_cfg;

	struct sockaddr_storage src_addr;
	int port;

	struct
	{
		unsigned int is_v6:1;
		unsigned int is_enabled:1;
	} flags;
} CommEvUDPClientConf;
/*******************************************************/
typedef struct _CommEvUDPClient
{
	struct _EvKQBase *kq_base;
	EvKQBaseLogSub log_sub;
	CommEvUDPClientConf config;

	int timer_id;

	struct sockaddr_storage addr;
	int cur_packet_id;
	int socket_fd;
	struct sockaddr_storage addr_me;
	int port;

//	CommEvRadiusStatsInfo stats;

	struct
	{
		unsigned int initialized:1;
	} flags;

} CommEvUDPClient;
static CommEvUDPClient *CommEvUDPClient_New(EvKQBase *kq_base);
static int CommEvUDPClient_Init(CommEvUDPClient *udp_client, CommEvUDPClientConf *client_cfg);
static EvBaseKQCBH CommEvUDPClient_TimerCB;
static EvBaseKQCBH CommEvUDPClient_EventRead;
static EvBaseKQCBH CommEvUDPClient_EventWrite;
static void TCPClientInitResolvBase(void);

EvKQBase *glob_ev_base;
EvDNSResolverBase *glob_ev_dns;
EvKQBaseLogBase *glob_log_base;
int glob_vote_timerid;

struct sockaddr_storage glob_dst_addr;
int glob_timerid;
/**************************************************************************************************************************/
int main(int argc, char **argv)
{
	CommEvUDPClient *ev_udpclient;
	CommEvUDPClientConf ev_udpclient_conf;
	EvKQBaseLogBaseConf log_conf;
	int op_status;

	/* Clean up stack */
	memset(&glob_dst_addr, 0, sizeof(struct sockaddr_storage));
	memset(&ev_udpclient_conf, 0, sizeof(CommEvUDPClientConf));
	memset(&log_conf, 0, sizeof(EvKQBaseLogBaseConf));

	log_conf.flags.double_write		= 1;

	/* Create event base */
	glob_ev_base	= EvKQBaseNew(NULL);
	glob_log_base	= EvKQBaseLogBaseNew(glob_ev_base, &log_conf);
	glob_ev_base->log_base 		= glob_log_base;

	ev_udpclient  				= CommEvUDPClient_New(glob_ev_base);

	char *host_ptr	= argc > 0 && argv[1] ? argv[1] : "127.0.0.1";
	char *port_ptr	= argc > 1 && argv[2] ? argv[2] : "3555";

	ev_udpclient_conf.port 					= atoi(port_ptr);

	if (ev_udpclient_conf.port <= 0)
		ev_udpclient_conf.port 				= 3555;

	BrbIsValidIpToSockAddr(host_ptr, &glob_dst_addr);
	BrbSockAddrSetPort(&glob_dst_addr, ev_udpclient_conf.port);

	ev_udpclient_conf.log_cfg.log_base 		= glob_log_base;
	ev_udpclient_conf.log_cfg.log_level 	= LOGTYPE_INFO;

//	BrbIsValidIpToSockAddr("192.168.155.101", &ev_udpclient_conf.src_addr);

	CommEvUDPClient_Init(ev_udpclient, &ev_udpclient_conf);

	/* Schedule send request timer */
	glob_timerid 		= EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 2000, CommEvUDPClient_TimerCB, ev_udpclient);

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "UDP CLIENT begin on PID [%d] - PORT [%s][%d]\n", getpid(), host_ptr, ev_udpclient_conf.port);

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, 100);

	exit(0);
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void TCPClientInitResolvBase(void)
{
	EvDNSResolverConf dns_conf;

	memset(&dns_conf, 0, sizeof(EvDNSResolverConf));

	/* Fill up DNS configuration */
	dns_conf.dns_ip_str			= "8.8.8.8";
	dns_conf.dns_port			= 53;
	dns_conf.lookup_timeout_ms	= 500;
	dns_conf.retry_timeout_ms	= 50;
	dns_conf.retry_count		= 10;

	glob_ev_dns	= CommEvDNSResolverBaseNew(glob_ev_base, &dns_conf);

	return;
}
/**************************************************************************************************************************/
static int CommEvUDPClient_TimerCB(int fd, int data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvUDPClient *udp_client 	= cb_data;

	glob_timerid 					= -1;

	EvKQBaseSetEvent(udp_client->kq_base, udp_client->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvUDPClient_EventWrite, udp_client);

	return 1;
}
/**************************************************************************************************************************/
static CommEvUDPClient *CommEvUDPClient_New(EvKQBase *kq_base)
{
	CommEvUDPClient *udp_client;

	/* Create new base and save reference with parent KQ_BASE */
	udp_client						= calloc(1, sizeof(CommEvUDPClient));
	udp_client->kq_base				= kq_base;
	udp_client->socket_fd 			= -1;
	udp_client->timer_id			= -1;

	return udp_client;
}
/**************************************************************************************************************************/
static int CommEvUDPClient_Init(CommEvUDPClient *udp_client, CommEvUDPClientConf *client_cfg)
{
	;
	int op_status;

	/* Sanitize */
	if (!udp_client)
		return -1;

	/* Already initialized */
	if (udp_client->flags.initialized)
		return -2;

	udp_client->flags.initialized 		= 1;

	/* Has configuration */
	if (client_cfg)
		memcpy((CommEvUDPClientConf *)&udp_client->config, client_cfg, sizeof(CommEvUDPClientConf));

	EvKQBaseLogSubApply(&udp_client->log_sub, &udp_client->config.log_cfg);

	if (udp_client->config.port <= 0)
		udp_client->config.port 	= (rand() % 50000) + 10000;

//	udp_client->socket_fd 			= EvKQBaseSocketCustomNew(udp_client->ev_base, udp_client->config.flags.is_v6 ? AF_INET6 : AF_INET);
	udp_client->socket_fd 			= EvKQBaseSocketUDPExt(udp_client->kq_base, udp_client->config.flags.is_v6 ? AF_INET6 : AF_INET);

	/* Set socket to REUSE_ADDR flag */
	op_status 						= EvKQBaseSocketSetReuseAddr(udp_client->kq_base, udp_client->socket_fd);

	/* Failed setting flag */
	if (op_status < 0)
	{
		KQBASE_LOGSUB_PRINTF(&udp_client->log_sub, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed setting REUSE_ADDR on FD [%d] - ERRNO [%d / %s]\n", udp_client->socket_fd, errno, strerror(errno));
		close(udp_client->socket_fd);
		return -2;
	}

	/* Set socket to REUSE_ADDR flag */
	op_status 	= EvKQBaseSocketSetReusePort(udp_client->kq_base, udp_client->socket_fd);

	/* Failed setting flag */
	if (op_status < 0)
	{
		KQBASE_LOG_PRINTF(udp_client->kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed setting REUSE_PORT on FD [%d] - ERRNO [%d / %s]\n", udp_client->socket_fd, errno, strerror(errno));
		close(udp_client->socket_fd);
		return -3;
	}

	/* Set socket to DSTADDR flag */
	op_status 	= EvKQBaseSocketSetDstAddr(udp_client->kq_base, udp_client->socket_fd);

	/* Failed setting flag */
	if (op_status < 0)
	{
		KQBASE_LOGSUB_PRINTF(&udp_client->log_sub, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed setting DSTADDR on FD [%d] - ERRNO [%d / %s]\n", udp_client->socket_fd, errno, strerror(errno));
		close(udp_client->socket_fd);
		return -3;
	}

	/* Initialize receiving socket on the device chosen */
	memset(&udp_client->addr_me, 0, sizeof(struct sockaddr_storage));

	if (udp_client->config.flags.is_v6)
	{
		udp_client->addr_me.ss_family								= AF_INET6;
#ifndef IS_LINUX
		udp_client->addr_me.ss_len									= sizeof(struct sockaddr_in6);
#endif
		((struct sockaddr_in6 *)&udp_client->addr_me)->sin6_port	= htons(udp_client->config.port);
		((struct sockaddr_in6 *)&udp_client->addr_me)->sin6_addr 	= in6addr_any;

//		/* Copy BINDIP if selected */
//		if (bindip)
//			memcpy(&((struct sockaddr_in6 *)&udp_client->addr_me)->sin6_addr, bindip, sizeof(struct in6_addr));

		op_status 	= bind(udp_client->socket_fd, (struct sockaddr *)&udp_client->addr_me, sizeof(struct sockaddr_in6));
	}
	else
	{
		udp_client->addr_me.ss_family								= AF_INET;
#ifndef IS_LINUX
		udp_client->addr_me.ss_len									= sizeof(struct sockaddr_in);
#endif
		((struct sockaddr_in *)&udp_client->addr_me)->sin_port		= htons(udp_client->config.port);

//		/* Copy BINDIP if selected */
//		if (bindip)
//			memcpy(&((struct sockaddr_in *)&udp_client->addr_me)->sin_addr, bindip, sizeof(struct in_addr));

		op_status 	= bind(udp_client->socket_fd, (struct sockaddr *)&udp_client->addr_me, sizeof(struct sockaddr_in));
	}

	/* Bind to UDP port */
	if (-1 == op_status)
	{
		char ip_str[64] = {0};

		if (udp_client->config.flags.is_v6)
			inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&udp_client->addr_me)->sin6_addr, (char *)&ip_str, INET6_ADDRSTRLEN);
		else
			inet_ntop(AF_INET, &((struct sockaddr_in *)&udp_client->addr_me)->sin_addr, (char *)&ip_str, INET_ADDRSTRLEN);

		KQBASE_LOGSUB_PRINTF(&udp_client->log_sub, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed binding on FD [%d] - ADDR [%d]-[%s] - ERRNO [%d / %s]\n",
				udp_client->socket_fd, udp_client->config.flags.is_v6, (char *)&ip_str, errno, strerror(errno));

		close(udp_client->socket_fd);
		udp_client->socket_fd 		= -1;
		return -4;
	}

	EvKQBaseSocketSetNonBlock(udp_client->kq_base, udp_client->socket_fd);

	/* Set socket description */
	EvKQBaseFDDescriptionSetByFD(udp_client->kq_base, udp_client->socket_fd , "BRB_EV_COMM - Radius CLIENT");

	/* Initialize timeout timer */
	udp_client->timer_id			= -1;

	KQBASE_LOGSUB_PRINTF(&udp_client->log_sub, LOGTYPE_WARNING, LOGCOLOR_GREEN, "FD [%d] - New CLIENT [%s]\n", udp_client->socket_fd, udp_client->config.flags.is_v6 ? "v6" : "v4");

	EvKQBaseSetEvent(udp_client->kq_base, udp_client->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvUDPClient_EventRead, udp_client);

	return 0;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvUDPClient_EventRead(int fd, int can_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvUDPClient *udp_client 	= cb_data;

	/* Read data from socket */
	struct sockaddr_storage	src_sa;
	struct sockaddr_storage	dst_sa;
	socklen_t sizeof_src 			= sizeof(src_sa);
	socklen_t sizeof_dst 			= sizeof(dst_sa);

	char buffer_str[4096];
	int op_status;

	int if_index 					= 0;
	int flags 						= 0;
	int read_sz;

	KQBASE_LOGSUB_PRINTF(&udp_client->log_sub, LOGTYPE_WARNING, LOGCOLOR_GREEN, "FD [%d]/[%d] - READ [%d]\n", fd, udp_client->socket_fd, can_read_sz);

	/* Reschedule read event */
	EvKQBaseSetEvent(udp_client->kq_base, fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvUDPClient_EventRead, udp_client);

	/* Receive accounting request */
	read_sz							= brb_recvfromto(fd, &buffer_str, sizeof(buffer_str), flags, &src_sa, &sizeof_src, &dst_sa, &sizeof_dst, &if_index);

	/* Failed reading, stop */
	if (read_sz <= 0)
	{
		KQBASE_LOGSUB_PRINTF(&udp_client->log_sub, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - READ_SIZE [%d] - Failed reading [%d]\n", fd, read_sz, errno);
		return 1;
	}

	KQBASE_LOGSUB_PRINTF(&udp_client->log_sub, LOGTYPE_WARNING, LOGCOLOR_GREEN, "READ [%d] of [%d]\n", read_sz, can_read_sz);

	EvKQBaseLoggerHexDump(glob_log_base, LOGTYPE_WARNING, (char *)&buffer_str, read_sz, 8, 4);

	return 0;
}
/**************************************************************************************************************************/
/* STATIC FUNCTIONS */
/**************************************************************************************************************************/
static int CommEvUDPClient_EventWrite(int fd, int can_write_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvUDPClient *udp_client			= cb_data;
//	struct sockaddr_storage	src_xx;
//	memset(&src_xx, 0, sizeof(struct sockaddr_storage));
//	struct sockaddr_storage	*src_sa 	= &src_xx;
	struct sockaddr_storage	*src_sa 	= &udp_client->addr_me;
	struct sockaddr_storage	*dst_sa 	= &glob_dst_addr;
	int if_index;

	char buffer_str[4096];
	int buffer_sz;

	int wrote_bytes 				= 0;
	int total_wrote_bytes			= 0;
	char *message_ptr 				= "A1B2C3D4E5A1B2C3D4E5A1B2C3D4E5A1B2C3D4E5A1B2C3D4E5";

	buffer_sz 						= strlcpy((char *)&buffer_str, message_ptr, sizeof(buffer_str) - 1);

	/* Write to socket */
	socklen_t sizeof_src 			= ((src_sa->ss_family == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in));
	socklen_t sizeof_dst 			= ((dst_sa->ss_family == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in));
	int flags 						= 0;


	if (dst_sa->ss_family == AF_INET6)
	{
#ifndef IS_LINUX
		dst_sa->ss_len					= sizeof(struct sockaddr_in6);
#endif
	}
	else
	{
#ifndef IS_LINUX
		dst_sa->ss_len					= sizeof(struct sockaddr_in);
#endif
	}
	sizeof_src = 0;
	EvKQBaseLoggerHexDump(glob_log_base, LOGTYPE_WARNING, (char *)src_sa, sizeof(struct sockaddr_storage), 8, 4);
	EvKQBaseLoggerHexDump(glob_log_base, LOGTYPE_WARNING, (char *)dst_sa, sizeof(struct sockaddr_storage), 8, 4);

	KQBASE_LOGSUB_PRINTF(&udp_client->log_sub, LOGTYPE_DEBUG, LOGCOLOR_ORANGE, "FD [%d] - TRY [%d] - SZ [%d]/[%d]\n",
			fd, buffer_sz, sizeof_src, sizeof_dst);

	wrote_bytes					= brb_sendfromto(fd, &buffer_str, buffer_sz, flags, (struct sockaddr *)src_sa, sizeof_src, (struct sockaddr *)dst_sa, sizeof_dst, if_index);

	/* Failed writing, enqueue it back and bail out */
	if (wrote_bytes <= 0)
	{

		KQBASE_LOGSUB_PRINTF(&udp_client->log_sub, LOGTYPE_WARNING, LOGCOLOR_ORANGE, "FD [%d] - drop - errno [%d] - return [%d]-[%d/%d]\n",
				fd, errno, wrote_bytes, src_sa->ss_family, dst_sa->ss_family);
	}
	else
	{
		KQBASE_LOGSUB_PRINTF(&udp_client->log_sub, LOGTYPE_DEBUG, LOGCOLOR_ORANGE, "FD [%d] - wrote [%d]\n",
				fd, wrote_bytes);
	}

	/* Set read and write event for socket */
	EvKQBaseSetEvent(udp_client->kq_base, fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvUDPClient_EventRead, udp_client);

	return 0;
}
/**************************************************************************************************************************/
