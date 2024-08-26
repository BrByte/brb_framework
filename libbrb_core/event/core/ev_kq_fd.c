/*
 * ev_kq_fd.c
 *
 *  Created on: 2012-09-08
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2012 BrByte Software (Oliveira Alves & Amorim LTDA)
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

static int EvKQBaseFDCloseJobAdd(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd);
static int EvKQBaseFDDoClose(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd);
static EvBaseKQJobCBH EvKQBaseFDCloseJob;

/**************************************************************************************************************************/
void EvKQBaseFDArenaNew(EvKQBase *kq_base)
{
	/* Initialize KQ_FD arena and list */
	kq_base->fd.arena = MemArenaNew(1024, (sizeof(EvBaseKQFileDesc) + 1), 128, BRBDATA_THREAD_UNSAFE);
	DLinkedListInit(&kq_base->fd.list, BRBDATA_THREAD_UNSAFE);
	return;
}
/**************************************************************************************************************************/
void EvKQBaseFDArenaDestroy(EvKQBase *kq_base)
{
	EvBaseKQFileDesc *kq_fd;

	/* Walk list */
	while ((kq_fd = DLinkedListPopHead(&kq_base->fd.list)))
	{
		/* Clear KQ_FD description */
		EvKQBaseFDDescriptionClearByKQFD(kq_base, kq_fd);
		continue;
	}

	/* Release arena-related data */
	MemArenaDestroy(kq_base->fd.arena);
	DLinkedListReset(&kq_base->fd.list);
	kq_base->fd.arena = NULL;
	return;
}
/**************************************************************************************************************************/
EvBaseKQFileDesc *EvKQBaseFDGrabFromArena(EvKQBase *kq_base, int fd)
{
	EvBaseKQFileDesc *kq_fd;
	int i;

	BRB_ASSERT(kq_base, (kq_base->fd.arena), "Trying to grab from NULL FD arena\n");

	/* Sanity check */
	if (fd < 0)
		return NULL;

	/* Grab FD */
	kq_fd = MemArenaGrabByID(kq_base->fd.arena, fd);

	/* Mark this FD as active and initialize data */
	if (!kq_fd->flags.active)
	{
		/* Clean up JOB info */
		kq_fd->notify.close.job_id		= -1;
		kq_fd->notify.close.ioloop_id	= -1;

		/* Set FD index */
		kq_fd->fd.num					= fd;

		/* Clean all timeout info */
		for (i = 0; i < KQ_CB_TIMEOUT_LASTITEM; i++)
		{
			/* Touch cb_ptr and cb_data */
			kq_fd->timeout[i].cb_handler_ptr	= NULL;
			kq_fd->timeout[i].cb_data_ptr		= NULL;

			/* Set it to disabled */
			kq_fd->timeout[i].when_ts			= -1;
			kq_fd->timeout[i].timeout_ms		= -1;
			kq_fd->timeout[i].timer_id 			= -1;
			continue;
		}
	}

	return kq_fd;
}
/**************************************************************************************************************************/
int EvKQBaseFDEventInvoke(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd, int ev_code, int ev_sz, int thrd_id, void *parent)
{
	EvBaseKQGenericEventPrototype *ev_proto	= &kq_fd->cb_handler[ev_code];
	EvBaseKQCBH *cb_handler					= ev_proto->cb_handler_ptr;
	void *cb_data							= ev_proto->cb_data_ptr;
	int ev_enabled							= ev_proto->flags.enabled;
	int ev_return							= 0;
	int fd									= kq_fd->fd.num;

	KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - EV_CODE [%d] - EV_SZ [%d]\n", fd, ev_code, ev_sz);

	/* Event disabled */
	if (!ev_enabled)
		return 0;

	/* No CB_H for this event on this FD */
	if (!cb_handler)
		return 0;

	/* Event disabled, wont run */
	if (!ev_enabled)
		return 0;

	/* Volatile event, disable */
	if (!ev_proto->flags.persist)
		ev_proto->flags.enabled	= 0;

	/* Touch time_stamp */
	memcpy(&ev_proto->run.tv, &kq_base->stats.cur_invoke_tv, sizeof(struct timeval));

	/* Clean up TIMEOUT event handlers */
	switch (ev_code)
	{
	case KQ_CB_HANDLER_WRITE:
	{
		/* Disable any timeout related info */
		EvKQBaseTimeoutClearByKQFD(kq_base, kq_fd, COMM_EV_TIMEOUT_WRITE);
		EvKQBaseTimeoutClearByKQFD(kq_base, kq_fd, COMM_EV_TIMEOUT_BOTH);
		break;
	}
	case KQ_CB_HANDLER_READ:
	{
		EvKQBaseTimeoutClearByKQFD(kq_base, kq_fd, COMM_EV_TIMEOUT_READ);
		EvKQBaseTimeoutClearByKQFD(kq_base, kq_fd, COMM_EV_TIMEOUT_BOTH);
		break;
	}
	}

	/* Jump into event handler */
	ev_return = cb_handler(kq_fd->fd.num, ev_sz, -1, cb_data, kq_base);

	return ev_return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
int EvKQBaseFDGenericInit(EvKQBase *kq_base, int fd, EvBaseFDType type)
{
	EvBaseKQFileDesc *kq_fd;

	/* Sanity check */
	if (fd < 0)
		return 0;

	/* Sanity check */
	if ((type < FD_TYPE_NONE) || (type >= FD_TYPE_LASTITEM))
		return 0;

	/* Grab FD from reference table - Clean up FD */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	/* Full defer data reset and flags clean up */
	EvKQBaseDeferResetByKQFD(kq_base, kq_fd);
	memset(&kq_fd->flags, 0, sizeof(kq_fd->flags));

	/* Set type as SERIAL */
	kq_fd->fd.type			= type;
	kq_fd->flags.active 	= 1;
	kq_fd->flags.closed		= 0;
	kq_fd->flags.closing	= 0;

	return 1;
}
/**************************************************************************************************************************/
int EvKQBaseFileFDInit(EvKQBase *kq_base, int fd)
{
	/* Sanity check */
	if (fd < 0)
		return 0;

	/* Invoke GENERIC INIT */
	EvKQBaseFDGenericInit(kq_base, fd, FD_TYPE_FILE);
	return 1;
}
/**************************************************************************************************************************/
int EvKQBaseSerialPortFDInit(EvKQBase *kq_base, int fd)
{
	/* Sanity check */
	if (fd < 0)
		return 0;

	/* Invoke GENERIC INIT */
	EvKQBaseFDGenericInit(kq_base, fd, FD_TYPE_SERIALPORT);
	return 1;
}
/**************************************************************************************************************************/
int EvKQBaseSocketGenericNew(EvKQBase *kq_base, int af, int style, int protocol, int type)
{
	int socket_fd;

	/* Ask the kernel for a socket */
	socket_fd = socket(af, style, protocol);

	if (socket_fd < 0)
		return socket_fd;

	/* Initialize internal KQ_FD */
	EvKQBaseFDGenericInit(kq_base, socket_fd, type);

	return socket_fd;
}
/**************************************************************************************************************************/
int EvKQBaseSocketRouteNew(EvKQBase *kq_base, int proto)
{
	/* Initialize internal KQ_FD */
	return EvKQBaseSocketGenericNew(kq_base, PF_ROUTE, SOCK_RAW, proto, FD_TYPE_ROUTE_SOCKET);
}
/**************************************************************************************************************************/
int EvKQBaseSocketNetmapNew(EvKQBase *kq_base, int flags)
{
	int socket_fd;

	/* Ask the kernel for a socket */
	socket_fd 		= open("/dev/netmap", flags);

	if (socket_fd < 0)
		return socket_fd;

	/* Initialize internal KQ_FD */
	EvKQBaseFDGenericInit(kq_base, socket_fd, FD_TYPE_NETMAP_SOCKET);

	return socket_fd;
}
/**************************************************************************************************************************/
int EvKQBaseSocketUDPNew(EvKQBase *kq_base)
{
	/* Initialize internal KQ_FD */
	return EvKQBaseSocketGenericNew(kq_base, AF_INET, SOCK_DGRAM, IPPROTO_IP, FD_TYPE_UDP_SOCKET);
}
/**************************************************************************************************************************/
int EvKQBaseSocketUDPExt(EvKQBase *kq_base, int af)
{
	return EvKQBaseSocketGenericNew(kq_base, af, SOCK_DGRAM, IPPROTO_UDP, FD_TYPE_UDP_SOCKET);
}
/**************************************************************************************************************************/
int EvKQBaseSocketRawNew(EvKQBase *kq_base)
{
	/* Initialize internal KQ_FD */
	return EvKQBaseSocketGenericNew(kq_base, AF_INET, SOCK_RAW, IPPROTO_RAW, FD_TYPE_RAW_SOCKET);
}
/**************************************************************************************************************************/
int EvKQBaseSocketCustomNew(EvKQBase *kq_base, int v6)
{
	/* Initialize internal KQ_FD */
	return EvKQBaseSocketGenericNew(kq_base, v6 ? AF_INET6 : AF_INET, SOCK_DGRAM, IPPROTO_IP, FD_TYPE_RAW_SOCKET);
}
/**************************************************************************************************************************/
int EvKQBaseSocketRAWNew(EvKQBase *kq_base, int af, int proto)
{
	/* Initialize internal KQ_FD */
	return EvKQBaseSocketGenericNew(kq_base, (af >= AF_UNSPEC) ? af : AF_INET, SOCK_RAW, proto, FD_TYPE_RAW_SOCKET);
}
/**************************************************************************************************************************/
int EvKQBaseSocketTCPNew(EvKQBase *kq_base)
{
	/* Initialize internal KQ_FD */
	return EvKQBaseSocketGenericNew(kq_base, AF_INET, SOCK_STREAM, IPPROTO_IP, FD_TYPE_TCP_SOCKET);
}
/**************************************************************************************************************************/
int EvKQBaseSocketTCPv6New(EvKQBase *kq_base)
{
	/* Initialize internal KQ_FD */
	return EvKQBaseSocketGenericNew(kq_base, AF_INET6, SOCK_STREAM, IPPROTO_IP, FD_TYPE_TCP_SOCKET);
}
/**************************************************************************************************************************/
int EvKQBaseSocketUNIXNew(EvKQBase *kq_base)
{
	/* Initialize internal KQ_FD */
	return EvKQBaseSocketGenericNew(kq_base, AF_UNIX, SOCK_STREAM, IPPROTO_IP, FD_TYPE_UNIX_SOCKET);
}
/**************************************************************************************************************************/
int EvKQBaseSocketUDPNewAndBind(EvKQBase *kq_base, struct in_addr *bindip, unsigned short port)
{
	struct sockaddr_in addr_me;
	int op_status;
	int fd;

	/* Try to create a new server socket */
	fd	= EvKQBaseSocketUDPNew(kq_base);

	/* Failed creating FD */
	if (fd < 0)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed initializing UDP FD - ERRNO [%d / %s]\n", errno, strerror(errno));
		return -1;
	}

	/* Set socket to REUSE_ADDR flag */
	op_status = EvKQBaseSocketSetReuseAddr(kq_base, fd);

	/* Failed setting flag */
	if (op_status < 0)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed setting REUSE_ADDR on FD [%d] - ERRNO [%d / %s]\n", fd, errno, strerror(errno));
		close(fd);
		return -2;
	}

	/* Set socket to REUSE_ADDR flag */
	op_status = EvKQBaseSocketSetReusePort(kq_base, fd);

	/* Failed setting flag */
	if (op_status < 0)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed setting REUSE_PORT on FD [%d] - ERRNO [%d / %s]\n", fd, errno, strerror(errno));
		close(fd);
		return -3;
	}

	/* Initialize receiving socket on the device chosen */
	memset((char *)&addr_me, 0, sizeof(addr_me));
	addr_me.sin_family		= AF_INET;
	addr_me.sin_port		= htons(port);

	/* Copy BINDIP if selected */
	if (bindip)
		memcpy(&(addr_me.sin_addr), bindip, COMM_IPV4_ALEN);

	/* Bind to UDP port */
	if (-1 == bind(fd, (struct sockaddr *)&addr_me, sizeof(addr_me)))
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed binding on FD [%d] - ADDR [%s] - ERRNO [%d / %s]\n",
				fd, inet_ntoa(addr_me.sin_addr), errno, strerror(errno));
		close(fd);
		return -4;
	}

	KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Initialized UDP Socket FD [%d] on PORT [%d]\n", fd, port);

	return fd;
}
/**************************************************************************************************************************/
int EvKQBaseSocketUDPExtAndBind(EvKQBase *kq_base, struct in_addr *bindip, unsigned short port, int af)
{
	struct sockaddr_storage addr_me;
	int op_status;
	int fd;

	/* Try to create a new server socket */
//	fd			= EvKQBaseSocketCustomNew(kq_base, v6);
	fd			= EvKQBaseSocketUDPExt(kq_base, af == AF_INET6 ? AF_INET6 : AF_INET);

	/* Failed creating FD */
	if (fd < 0)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed initializing UDP FD - ERRNO [%d / %s]\n", errno, strerror(errno));
		return -1;
	}

	/* Set socket to REUSE_ADDR flag */
	op_status 	= EvKQBaseSocketSetReuseAddr(kq_base, fd);

	/* Failed setting flag */
	if (op_status < 0)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed setting REUSE_ADDR on FD [%d] - ERRNO [%d / %s]\n", fd, errno, strerror(errno));
		close(fd);
		return -2;
	}

	/* Set socket to REUSE_ADDR flag */
	op_status 	= EvKQBaseSocketSetReusePort(kq_base, fd);

	/* Failed setting flag */
	if (op_status < 0)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed setting REUSE_PORT on FD [%d] - ERRNO [%d / %s]\n", fd, errno, strerror(errno));
		close(fd);
		return -3;
	}

	/* Set socket to REUSE_ADDR flag */
	op_status 	= EvKQBaseSocketSetDstAddr(kq_base, fd);

	/* Failed setting flag */
	if (op_status < 0)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed setting REUSE_PORT on FD [%d] - ERRNO [%d / %s]\n", fd, errno, strerror(errno));
		close(fd);
		return -3;
	}

	/* Initialize receiving socket on the device chosen */
	memset(&addr_me, 0, sizeof(struct sockaddr_storage));

	if (af == AF_INET6)
	{
		addr_me.ss_family								= AF_INET6;
#ifndef IS_LINUX
		addr_me.ss_len									= sizeof(struct sockaddr_in6);
#endif
		satosin6(&addr_me)->sin6_port					= htons(port);
		satosin6(&addr_me)->sin6_addr 					= in6addr_any;

		/* Copy BINDIP if selected */
		if (bindip)
			memcpy(&satosin6(&addr_me)->sin6_addr, bindip, sizeof(struct in6_addr));

		op_status 	= bind(fd, (struct sockaddr *)&addr_me, sizeof(struct sockaddr_in6));
	}
	else
	{
		addr_me.ss_family								= AF_INET;
#ifndef IS_LINUX
		addr_me.ss_len									= sizeof(struct sockaddr_in);
#endif
		satosin(&addr_me)->sin_port						= htons(port);

		/* Copy BINDIP if selected */
		if (bindip)
			memcpy(&satosin(&addr_me)->sin_addr, bindip, sizeof(struct in_addr));

		op_status 	= bind(fd, (struct sockaddr *)&addr_me, sizeof(struct sockaddr_in));
	}

	/* Bind to UDP port */
	if (-1 == op_status)
	{
		char ip_str[64] = {0};

		if (addr_me.ss_family == AF_INET6)
			inet_ntop(AF_INET6, &satosin6(&addr_me)->sin6_addr, (char *)&ip_str, INET6_ADDRSTRLEN);
		else
			inet_ntop(AF_INET, &satosin(&addr_me)->sin_addr, (char *)&ip_str, INET_ADDRSTRLEN);

		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed binding on FD [%d] - ADDR [%d]-[%s] - ERRNO [%d / %s]\n",
				fd, addr_me.ss_family, (char *)&ip_str, errno, strerror(errno));

		close(fd);
		return -4;
	}

	KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_DEBUG, LOGCOLOR_GREEN, "Initialized UDP Socket FD [%d] on PORT [%d]\n", fd, port);

	return fd;
}
/**************************************************************************************************************************/
int EvKQBaseSocketRawNewAndBind(EvKQBase *kq_base)
{
	struct sockaddr_in addr_me;
	int op_status;
	int fd;

	/* Try to create a new server socket */
	fd	= EvKQBaseSocketRawNew(kq_base);

	/* Failed creating FD */
	if (fd < 0)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed initializing UDP FD - ERRNO [%d / %s]\n", errno, strerror(errno));
		return -1;
	}

	/* Set socket to REUSE_ADDR flag */
	op_status = EvKQBaseSocketSetReuseAddr(kq_base, fd);

	/* Failed setting flag */
	if (op_status < 0)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed setting REUSE_ADDR on FD [%d] - ERRNO [%d / %s]\n", fd, errno, strerror(errno));
		close(fd);
		return -2;
	}

	/* Set socket to use BROADCAST */
	op_status = EvKQBaseSocketSetBroadcast(kq_base, fd);

	/* Failed setting flag */
	if (op_status < 0)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed setting REUSE_PORT on FD [%d] - ERRNO [%d / %s]\n", fd, errno, strerror(errno));
		close(fd);
		return -4;
	}

	/* Set socket to REUSE_ADDR flag */
	op_status = EvKQBaseSocketSetReusePort(kq_base, fd);

	/* Failed setting flag */
	if (op_status < 0)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed setting REUSE_PORT on FD [%d] - ERRNO [%d / %s]\n", fd, errno, strerror(errno));
		close(fd);
		return -4;
	}

	KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Initialized RAW Socket FD [%d]\n", fd);

	return fd;
}
/**************************************************************************************************************************/
int EvKQBaseSocketCustomNewAndBind(EvKQBase *kq_base, struct in_addr *bindip, unsigned short port, int v6)
{
	struct sockaddr_storage addr_me;
	int op_status;
	int fd;

	/* Try to create a new server socket */
	fd			= EvKQBaseSocketCustomNew(kq_base, v6);

	/* Failed creating FD */
	if (fd < 0)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed initializing UDP FD - ERRNO [%d / %s]\n", errno, strerror(errno));
		return -1;
	}

	/* Set socket to REUSE_ADDR flag */
	op_status 	= EvKQBaseSocketSetReuseAddr(kq_base, fd);

	/* Failed setting flag */
	if (op_status < 0)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed setting REUSE_ADDR on FD [%d] - ERRNO [%d / %s]\n", fd, errno, strerror(errno));
		close(fd);
		return -2;
	}

	/* Set socket to REUSE_ADDR flag */
	op_status = EvKQBaseSocketSetReusePort(kq_base, fd);

	/* Failed setting flag */
	if (op_status < 0)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed setting REUSE_PORT on FD [%d] - ERRNO [%d / %s]\n", fd, errno, strerror(errno));
		close(fd);
		return -3;
	}

	/* Initialize receiving socket on the device chosen */
	memset(&addr_me, 0, sizeof(struct sockaddr_storage));

	if (v6)
	{
		addr_me.ss_family								= AF_INET6;
#ifndef IS_LINUX
		addr_me.ss_len									= sizeof(struct sockaddr_in6);
#endif
		((struct sockaddr_in6 *)&addr_me)->sin6_port	= htons(port);

		((struct sockaddr_in6 *)&addr_me)->sin6_addr 	= in6addr_any;

		/* Copy BINDIP if selected */
		if (bindip)
			memcpy(&((struct sockaddr_in6 *)&addr_me)->sin6_addr, bindip, sizeof(struct in6_addr));

		op_status 	= bind(fd, (struct sockaddr *)&addr_me, sizeof(struct sockaddr_in6));
	}
	else
	{
		addr_me.ss_family								= AF_INET;
#ifndef IS_LINUX
		addr_me.ss_len									= sizeof(struct sockaddr_in);
#endif
		((struct sockaddr_in *)&addr_me)->sin_port		= htons(port);

		/* Copy BINDIP if selected */
		if (bindip)
			memcpy(&((struct sockaddr_in *)&addr_me)->sin_addr, bindip, sizeof(struct in_addr));

		op_status 	= bind(fd, (struct sockaddr *)&addr_me, sizeof(struct sockaddr_in));
	}

	/* Bind to UDP port */
	if (-1 == op_status)
	{
		char ip_str[64] = {0};

		if (v6)
			inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&addr_me)->sin6_addr, (char *)&ip_str, INET6_ADDRSTRLEN);
		else
			inet_ntop(AF_INET, &((struct sockaddr_in *)&addr_me)->sin_addr, (char *)&ip_str, INET_ADDRSTRLEN);

		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed binding on FD [%d] - ADDR [%d]-[%s] - ERRNO [%d / %s]\n",
				fd, v6, (char *)&ip_str, errno, strerror(errno));
		close(fd);
		return -4;
	}

	KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Initialized UDP Socket FD [%d] on PORT [%d]\n", fd, port);

	return fd;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
void EvKQBaseSocketClose(EvKQBase *kq_base, int fd)
{
	EvBaseKQFileDesc *kq_fd;
	int op_status;

	char *desc_str = EvKQBaseFDDescriptionGetByFD(kq_base, fd);

	/* Do not allow invalid FDs in this routine */
	if (fd < 0)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "FD [%d] - DESC [%s] - Called to close negative FD\n",
				fd, desc_str);
		return;
	}

	/* Grab FD from reference table - Set flags as CLOSING */
	kq_fd					= EvKQBaseFDGrabFromArena(kq_base, fd);
	kq_fd->flags.closing	= 1;

	KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - LINGER [%d] - DESC [%s] - Begin closing\n",
			fd, kq_base->kq_conf.onoff.close_linger, desc_str);

	/* Schedule job for lingering close (will run on next IO loop) */
	if (kq_base->kq_conf.onoff.close_linger)
	{
		/* Try to add CLOSE JOB. Will close immediately if JOB_ADD fail */
		EvKQBaseFDCloseJobAdd(kq_base, kq_fd);
		return;
	}

	/* Actually close it */
	EvKQBaseFDDoClose(kq_base, kq_fd);
	return;
}
/**************************************************************************************************************************/
int EvKQBaseFDCleanupByKQFD(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd)
{
	/* Clean up events, timeout, defer and description */
	EvKQBaseClearEvents(kq_base, kq_fd->fd.num);
	EvKQBaseTimeoutClearAll(kq_base, kq_fd->fd.num);
	EvKQBaseDeferResetByKQFD(kq_base, kq_fd);
	EvKQBaseFDDescriptionClearByFD(kq_base, kq_fd->fd.num);

	/* Reset flags and notify data */
	memset(&kq_fd->flags, 0, sizeof(kq_fd->flags));
	kq_fd->notify.close.job_id		= -1;
	kq_fd->notify.close.ioloop_id	= -1;

	/* Set KQ_FQ as CLOSED */
	kq_fd->fd.type					= FD_TYPE_NONE;
	kq_fd->flags.closed				= 1;
	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
int EvKQBaseFDReadBufferDrain(EvKQBase *kq_base, int fd)
{
	char junk[8092];
	int op_status;

	/* Sanity check */
	if (fd < 0)
		return 0;

	while (1)
	{
		op_status = read(fd, &junk, sizeof(junk));

		/* Read buffer empty */
		if (op_status <= 0)
			break;

		continue;
	}

	return 1;
}
/**************************************************************************************************************************/
int EvKQBaseSocketBufferReadSizeGet(EvKQBase *kq_base, int fd)
{
	EvBaseKQFileDesc *kq_fd;
	int buffer_ret;
	int op_status;

	unsigned int buffer_sz = sizeof(buffer_ret);

	/* Sanity check */
	if (fd < 0)
		return 0;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	op_status = getsockopt(fd, SOL_SOCKET, SO_RCVBUF, (void*)&buffer_ret, &buffer_sz);
	return buffer_ret;
}
/**************************************************************************************************************************/
int EvKQBaseSocketBufferWriteSizeGet(EvKQBase *kq_base, int fd)
{
	EvBaseKQFileDesc *kq_fd;
	int buffer_ret;
	int op_status;

	unsigned int buffer_sz = sizeof(buffer_ret);

	/* Sanity check */
	if (fd < 0)
		return 0;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	op_status = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, (void*)&buffer_ret, &buffer_sz);
	return buffer_ret;
}
/**************************************************************************************************************************/
int EvKQBaseSocketSetNoDelay(EvKQBase *kq_base, int fd)
{
	EvBaseKQFileDesc *kq_fd;
	int on = 1;

	/* Sanity check */
	if (fd < 0)
		return 0;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *) &on, sizeof(on));
}
/**************************************************************************************************************************/
int EvKQBaseSocketSetBroadcast(EvKQBase *kq_base, int fd)
{
	EvBaseKQFileDesc *kq_fd;
	int on = 1;

	/* Sanity check */
	if (fd < 0)
		return 0;

	return (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof (on)));
}
/**************************************************************************************************************************/
int EvKQBaseSocketSetKeepAlive(EvKQBase *kq_base, int fd)
{
	EvBaseKQFileDesc *kq_fd;
	int on = 1;

	/* Sanity check */
	if (fd < 0)
		return 0;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	return setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char *) &on, sizeof(on));
}
/**************************************************************************************************************************/
int EvKQBaseSocketSetLinger(EvKQBase *kq_base, int fd, int linger_sec)
{
	EvBaseKQFileDesc *kq_fd;
	struct linger linger_data;

	/* Sanity check */
	if (fd < 0)
		return 0;

	/* Fill in linger data to zero to generate a reset when we close */
	linger_data.l_onoff		= 1;
	linger_data.l_linger	= linger_sec;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	return setsockopt(fd, SOL_SOCKET, SO_LINGER, (char *) &linger_data, sizeof(linger_data));
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
char *EvKQBaseFDDescriptionGetByFD(EvKQBase *kq_base, int fd)
{
	EvBaseKQFileDesc *kq_fd;

	/* Sanity check */
	if (fd < 0)
		return "Invalid";

	if (fd == 0)
		return "STDIN";
	else if (fd == 1)
		return "STDOUT";
	else if (fd == 2)
		return "STDERR";

	/* Grab FD from reference table and mark listening socket flags */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	/* Sanity check */
	if (!kq_fd)
		return "Invalid";

	/* Description not set */
	if (!kq_fd->fd.description_str)
		return "Not set";

	return kq_fd->fd.description_str;
}
/**************************************************************************************************************************/
void EvKQBaseFDDescriptionClearByKQFD(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd)
{
	/* Sanity check */
	if (!kq_fd)
		return;

	/* Description not set */
	if (!kq_fd->fd.description_str)
		return;

	/* Free and unlink */
	free(kq_fd->fd.description_str);
	kq_fd->fd.description_str	= NULL;
	kq_fd->fd.description_sz	= 0;

	return;
}
/**************************************************************************************************************************/
void EvKQBaseFDDescriptionClearByFD(EvKQBase *kq_base, int fd)
{
	EvBaseKQFileDesc *kq_fd;

	/* Sanity check */
	if (fd < 0)
		return;

	/* Grab FD from reference table and mark listening socket flags */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	/* Sanity check */
	if (!kq_fd)
		return;

	EvKQBaseFDDescriptionClearByKQFD(kq_base, kq_fd);

	return;
}
/**************************************************************************************************************************/
void EvKQBaseFDDescriptionSetByFD(EvKQBase *kq_base, int fd, char *description, ...)
{
	EvBaseKQFileDesc *kq_fd;
	char local_buf[256];
	va_list args;

	/* Sanity check */
	if (fd < 0)
		return;

	/* Grab FD from reference table and mark listening socket flags */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	/* Sanity check */
	if (!kq_fd)
		return;

	/* Clean kq_fd description area */
	if (kq_fd->fd.description_str)
	{
		free(kq_fd->fd.description_str);
		kq_fd->fd.description_str	= NULL;
		return;
	}

	/* Clean up stack */
	memset(&local_buf, 0, sizeof(local_buf));

	/* Print formatted description into kq_fd */
	va_start( args, description );
	kq_fd->fd.description_sz = vsnprintf((char*)&local_buf, (sizeof(local_buf) - 2), description, args);
	local_buf[kq_fd->fd.description_sz] = '\0';
	va_end(args);

	/* Load string */
	kq_fd->fd.description_str = strdup((char*)&local_buf);

	return;
}
/**************************************************************************************************************************/
void EvKQBaseFDDescriptionSet(EvBaseKQFileDesc *kq_fd, char *description, ...)
{
	char local_buf[256];
	va_list args;

	/* Sanity check */
	if (!kq_fd)
		return;

	/* Clean kq_fd description area */
	if (kq_fd->fd.description_str)
	{
		free(kq_fd->fd.description_str);
		kq_fd->fd.description_str	= NULL;
		return;
	}

	/* Print formatted description into kq_fd */
	va_start( args, description );
	kq_fd->fd.description_sz = vsnprintf((char*)&local_buf, (sizeof(local_buf) - 2), description, args);
	local_buf[kq_fd->fd.description_sz] = '\0';
	va_end(args);

	/* Load string */
	kq_fd->fd.description_str = strdup((char*)&local_buf);

	return;
}
/**************************************************************************************************************************/
void EvKQBaseFDDescriptionAppend(EvBaseKQFileDesc *kq_fd, char *description, ...)
{
	/* NOT IMPLEMENTED */
	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
int EvKQBaseSocketDrain(EvKQBase *kq_base, int fd)
{
	char junk_buf[8092];
	int op_status;

	long total_drained	= 0;

	while (1)
	{
		op_status = read(fd, &junk_buf, sizeof(junk_buf));

		/* Finished draining */
		if (op_status <= 0)
			break;

		total_drained += op_status;
		continue;
	}

	return total_drained;
}
/**************************************************************************************************************************/
/**************************************************************************************************************************/
int EvKQBaseSocketSetCloseOnExec(EvKQBase *kq_base, int fd)
{
	EvBaseKQFileDesc *kq_fd;
	int flags;
	int dummy = 0;

	/* Do not allow invalid FDs in this routine */
	if (fd < 0)
		return -1;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	/* We should be active if operator is trying to control this FD */
	kq_fd->flags.active		= 1;
	kq_fd->flags.closed 	= 0;
	kq_fd->flags.closing	= 0;

	if ((flags = fcntl(fd, F_GETFL, dummy)) < 0)
	{
		//debug(5, 0) ("FD %d: fcntl F_GETFL: %s\n", fd, xstrerror());
		return -1;
	}
	if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
	{
		//debug(5, 0) ("FD %d: set close-on-exec failed: %s\n", fd, xstrerror());
		return -1;
	}

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	/* Set flags */
	kq_fd->flags.so_closeonexec = 1;

	return 1;
}
/**************************************************************************************************************************/
int EvKQBaseSocketSetNonBlock(EvKQBase *kq_base, int fd)
{
	EvBaseKQFileDesc *kq_fd;
	int flags;

	/* Do not allow invalid FDs in this routine */
	if (fd < 0)
		return 0;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	/* We should be active if operator is trying to control this FD */
	kq_fd->flags.active		= 1;
	kq_fd->flags.closed 	= 0;
	kq_fd->flags.closing	= 0;

	flags = fcntl(fd, F_GETFL);

	if (flags < 0)
		return flags;

	flags |= O_NONBLOCK;

	if (fcntl(fd, F_SETFL, flags) < 0)
	{
		return -1;
	}
	else
	{
		/* Grab FD from reference table */
		kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

		/* Set flags */
		kq_fd->flags.so_nonblocking = 1;
	}

	return 0;
}
/**************************************************************************************************************************/
int EvKQBaseSocketSetBlocking(EvKQBase *kq_base, int fd)
{
	EvBaseKQFileDesc *kq_fd;
	int flags;

	/* Do not allow invalid FDs in this routine */
	if (fd < 0)
		return 0;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	/* We should be active if operator is trying to control this FD */
	kq_fd->flags.active		= 1;
	kq_fd->flags.closed 	= 0;
	kq_fd->flags.closing	= 0;

	flags = fcntl(fd, F_GETFL);

	if (flags < 0)
		return flags;

	flags ^= O_NONBLOCK;

	if (fcntl(fd, F_SETFL, flags) < 0)
	{
		return -1;
	}
	else
	{
		/* Grab FD from reference table */
		kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

		/* Set flags */
		kq_fd->flags.so_nonblocking = 0;

	}

	return 0;
}
/**************************************************************************************************************************/
int EvKQBaseSocketSetReuseAddr(EvKQBase *kq_base, int fd)
{
	EvBaseKQFileDesc *kq_fd;
	int reuseaddr_on = 1;

	/* Do not allow invalid FDs in this routine */
	if (fd < 0)
		return 0;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	/* We should be active if operator is trying to control this FD */
	kq_fd->flags.active		= 1;
	kq_fd->flags.closed 	= 0;
	kq_fd->flags.closing	= 0;

	/* Set sockopt SO_REUSEADDR */
	if ( setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_on, sizeof(reuseaddr_on)) == -1)
		return -1;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	/* Set flags */
	kq_fd->flags.so_reuse_port = 1;

	return 0;
}
/**************************************************************************************************************************/
int EvKQBaseSocketSetReusePort(EvKQBase *kq_base, int fd)
{
	EvBaseKQFileDesc *kq_fd;
	int reuseport_on = 1;

	/* Do not allow invalid FDs in this routine */
	if (fd < 0)
		return 0;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	/* We should be active if operator is trying to control this FD */
	kq_fd->flags.active		= 1;
	kq_fd->flags.closed 	= 0;
	kq_fd->flags.closing	= 0;

	/* Set sockopt SO_REUSEADDR */
	if ( setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reuseport_on, sizeof(reuseport_on)) == -1)
		return -1;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	/* Set flags */
	kq_fd->flags.so_reuse_port = 1;

	return 0;
}
/**************************************************************************************************************************/
int EvKQBaseSocketSetTCPBufferSize(EvKQBase *kq_base, int fd, int size)
{
	EvBaseKQFileDesc *kq_fd;
	int r, err = 1;
	int s = size;

	/* Do not allow invalid FDs in this routine */
	if (fd < 0)
		return 0;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	/* We should be active if operator is trying to control this FD */
	kq_fd->flags.active		= 1;
	kq_fd->flags.closed 	= 0;
	kq_fd->flags.closing	= 0;

	r = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char *) &s, sizeof(s));

	if (r < 0)
		err = 0;

	s = size;

	r = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char *) &s, sizeof(s));

	if (r < 0)
		err = 0;

	return err;
}
/**************************************************************************************************************************/
int EvKQBaseSocketSetDstAddr(EvKQBase *kq_base, int fd)
{
	EvBaseKQFileDesc *kq_fd;
	int proto 	= 0;
	int flag 	= 0;
	int opt 	= 1;
	struct sockaddr_storage si;
	socklen_t si_len = sizeof(si);
	int op_status;

	errno 		= ENOSYS;

	/* Do not allow invalid FDs in this routine */
	if (fd < 0)
		return 0;

	/* Grab FD from reference table */
	kq_fd 		= EvKQBaseFDGrabFromArena(kq_base, fd);

	/* We should be active if operator is trying to control this FD */
	kq_fd->flags.active		= 1;
	kq_fd->flags.closed 	= 0;
	kq_fd->flags.closing	= 0;

	memset(&si, 0, sizeof(si));

	if (getsockname(fd, (struct sockaddr *) &si, &si_len) < 0)
		return -1;

	if (si.ss_family == AF_INET)
	{
#ifdef HAVE_IP_PKTINFO
		/*
		 *	Linux
		 */
		proto 	= SOL_IP;
		flag 	= IP_PKTINFO;
#else
#  ifdef IP_RECVDSTADDR

		/*
		 *	Set the IP_RECVDSTADDR option (BSD).  Note:
		 *	IP_RECVDSTADDR == IP_SENDSRCADDR
		 */
		proto 	= IPPROTO_IP;
		flag 	= IP_RECVDSTADDR;
#  else
		return -1;
#  endif
#endif

#if defined(AF_INET6) && defined(IPV6_PKTINFO)
	}
	else if (si.ss_family == AF_INET6)
	{
		/* This should actually be standard IPv6 */
		proto 	= IPPROTO_IPV6;

		/* Work around Linux-specific hackery */
		flag 	= IPV6_RECVPKTINFO;
	}
	else
	{
#endif

		/* Unknown AF.  Return an error if possible */
#  ifdef EPROTONOSUPPORT
		errno = EPROTONOSUPPORT;
#  endif
		return -1;
	}

	/* Invoke the kernel */
	op_status		= setsockopt(fd, proto, flag, &opt, sizeof(opt));

	/* Failure */
	if (op_status < -1)
		return op_status;

//	/* Set flags */
//	kq_fd->flags.so_reuse_port = 1;

	return 0;
}
/**************************************************************************************************************************/
int EvKQBaseSocketSetTOS(EvKQBase *kq_base, int fd, int tos)
{
	EvBaseKQFileDesc *kq_fd;
	int op_status;

	/* Do not allow invalid FDs in this routine */
	if (fd < 0)
		return 0;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	/* We should be active if operator is trying to control this FD */
	kq_fd->flags.active		= 1;
	kq_fd->flags.closed 	= 0;
	kq_fd->flags.closing	= 0;

	/* TOS already set, bail out */
	if (kq_fd->fd.tos == tos)
		return 0;

	/* Invoke the kernel */
	op_status		= setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));

	/* Success */
	if (op_status > -1)
	{
		kq_fd->fd.tos	= tos;
		return 1;
	}


	return 0;
}
/**************************************************************************************************************************/
int EvKQBaseSocketGetTOS(EvKQBase *kq_base, int fd)
{
	EvBaseKQFileDesc *kq_fd;
	socklen_t len;
	int op_status;
	int tos;

	/* Do not allow invalid FDs in this routine */
	if (fd < 0)
		return 0;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	/* We should be active if operator is trying to control this FD */
	kq_fd->flags.active		= 1;
	kq_fd->flags.closed 	= 0;
	kq_fd->flags.closing	= 0;

	len = sizeof(tos);

	op_status = getsockopt(fd, IPPROTO_IP, IP_TOS, &tos, &len);

	if (op_status > -1)
	{
		return tos;
	}

	return 0;
}
/**************************************************************************************************************************/
int EvKQBaseSocketBindLocal(EvKQBase *kq_base, int fd, struct sockaddr *sock_addr)
{
	EvBaseKQFileDesc *kq_fd;

	/* Do not allow invalid FDs in this routine */
	if (fd < 0)
		return 0;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	/* Bind socket to local IP */
	if (bind(fd, sock_addr, (sock_addr->sa_family == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in)) != 0)
		return 0;

	/* We should be active if operator is trying to control this FD */
	kq_fd->flags.active		= 1;
	kq_fd->flags.closed 	= 0;
	kq_fd->flags.closing	= 0;

	return 1;
}
/**************************************************************************************************************************/
int EvKQBaseSocketBindRemote(EvKQBase *kq_base, int fd, struct sockaddr *sock_addr)
{
	EvBaseKQFileDesc *kq_fd;
	int on = 1;

	/* Do not allow invalid FDs in this routine */
	if (fd < 0)
		return 0;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

#if defined(__linux__)
	/* Invoke the kernel for a NON LOCAL BIND for this IP */
	if (setsockopt(fd, IPPROTO_IP, IP_FREEBIND, (char *)&on, sizeof(on)) != 0)
		return 0;
#else
	int proto = (sock_addr->sa_family == AF_INET6) ? IPPROTO_IPV6 : IPPROTO_IP;
	int bindtype = (sock_addr->sa_family == AF_INET6) ? IPV6_V6ONLY : IP_BINDANY;
	/* Invoke the kernel for a NON LOCAL BIND for this IP */
	if (setsockopt(fd, proto, bindtype, (char *)&on, sizeof(on)) != 0)
		return 0;
#endif

	/* Bind SOCKET to remote IP */
	if (bind(fd, sock_addr, (sock_addr->sa_family == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in)) != 0)
		return 0;

	/* We should be active if operator is trying to control this FD */
	kq_fd->flags.active		= 1;
	kq_fd->flags.closed 	= 0;
	kq_fd->flags.closing	= 0;

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int EvKQBaseFDCloseJobAdd(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd)
{
	char *desc_str = EvKQBaseFDDescriptionGetByFD(kq_base, kq_fd->fd.num);

	/* Add close JOB for next IO_LOOP */
	kq_fd->notify.close.job_id		= EvKQJobsAdd(kq_base, JOB_ACTION_ADD_VOLATILE, 1, EvKQBaseFDCloseJob, kq_fd);
	kq_fd->notify.close.ioloop_id	= kq_base->stats.kq_invoke_count;

	/* Failed adding CLOSE_JOB, close right now */
	if (kq_fd->notify.close.job_id < 0)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "FD [%d] - [%s] - Failed scheduling CLOSE_JOB. Immediate close\n", kq_fd->fd, desc_str);

		/* Actually close it */
		EvKQBaseFDDoClose(kq_base, kq_fd);
		return 0;
	}
	/* Close JOB added OK */
	else
	{
		/* Cancel events, timeouts and defer */
		EvKQBaseClearEvents(kq_base, kq_fd->fd.num);
		EvKQBaseTimeoutClearAll(kq_base, kq_fd->fd.num);
		EvKQBaseDeferResetByKQFD(kq_base, kq_fd);

		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Linger close JOB added at ID [%d]\n", kq_fd->fd, kq_fd->notify.close.job_id);
	}

	return 1;
}
/**************************************************************************************************************************/
static int EvKQBaseFDDoClose(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd)
{
	int op_status;
	char *desc_str = EvKQBaseFDDescriptionGetByFD(kq_base, kq_fd->fd.num);

	BRB_ASSERT_FMT(kq_base, (!kq_fd->flags.closed), "FD [%d] - CLOSE_JOBID [%d] - FD already closed!\n", kq_fd->fd.num, kq_fd->notify.close.job_id);

	/* Reset notify data */
	kq_fd->notify.close.job_id = -1;

	/* Actually close FD */
	op_status = close(kq_fd->fd.num);

	/* Failed closing FD */
	if ((op_status < 0) && errno != ECONNRESET)
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - TYPE [%d] - FLAGS [%u] - DESC [%s] - Close failed with ERRNO [%d]\n", kq_fd->fd.num, kq_fd->fd.type, kq_fd->fd.fflags, desc_str, errno);

	/* CleanUP KQ_FD and set flag as CLOSED */
	EvKQBaseFDCleanupByKQFD(kq_base, kq_fd);
	return 1;
}
/**************************************************************************************************************************/
static int EvKQBaseFDCloseJob(void *kq_job_ptr, void *cbdata_ptr)
{
	int op_status;

	EvKQQueuedJob *kq_job				= kq_job_ptr;
	EvBaseKQFileDesc *kq_fd				= cbdata_ptr;
	EvKQBase *ev_base					= kq_job->ev_base;
	char *desc_str						= EvKQBaseFDDescriptionGetByFD(ev_base, kq_fd->fd.num);
	int fd								= kq_fd->fd.num;

	KQBASE_LOG_PRINTF(ev_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - JOB_ID [%d] - Will close\n", fd, kq_job->job.id);
	BRB_ASSERT_FMT(ev_base, (kq_fd->notify.close.job_id == kq_job->job.id), "FD [%d] - Linger close JOB ID mispatch [%d / %d]\n",
			fd, kq_fd->notify.close.job_id, kq_job->job.id);

	BRB_ASSERT_FMT(ev_base, (kq_fd->flags.closing), "FD [%d] - JOB_ID [%d] - FD should be in CLOSING state\n", fd, kq_job->job.id);
	BRB_ASSERT_FMT(ev_base, (!kq_fd->flags.closed), "FD [%d] - JOB_ID [%d] - FD already closed!\n", fd, kq_job->job.id);

	/* Actually close it */
	EvKQBaseFDDoClose(ev_base, kq_fd);
	return 1;
}
/**************************************************************************************************************************/
