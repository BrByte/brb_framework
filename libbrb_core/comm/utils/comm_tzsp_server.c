/*
 * comm_tzsp_server.c
 *
 *  Created on: 2015-05-16
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2013 BrByte Software (Oliveira Alves & Amorim LTDA)
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
//#include "../include/libbrb_comm_utils.h"

#ifdef IS_LINUX
#include <linux/if_ether.h>

/* 802.1q Ether Header */
struct ether_vlan_header {
	u_int8_t ether_dhost[ETH_ALEN];
	u_int8_t ether_shost[ETH_ALEN];
	uint16_t evl_encap_proto;
	uint16_t evl_tag;
	uint16_t evl_proto;
};

#else
#include <net/if_bridgevar.h>
#include <net/if_llc.h>
#include <net/if_vlan_var.h>
#endif

/* ACL prototypes */
static CommEvTZSPServerACL *CommEvTZSPServerACLNew(CommEvTZSPServer *ev_tzsp_server, struct sockaddr_in addr);
static int CommEvTZSPServerACLDestroy(CommEvTZSPServerACL *acl);
static CommEvTZSPServerACL *CommEvTZSPServerACLAddrAdd(CommEvTZSPServer *ev_tzsp_server, struct sockaddr_in addr);
static CommEvTZSPServerACL *CommEvTZSPServerACLAddrFind(CommEvTZSPServer *ev_tzsp_server, struct sockaddr_in addr);

/* CONN prototypes */
static CommEvTZSPServerConn *CommEvTZSPServerConnNew(CommEvTZSPServer *ev_tzsp_server, struct sockaddr_in *addr);
static int CommEvTZSPServerConnDestroy(CommEvTZSPServerConn *conn);
static CommEvTZSPServerConn *CommEvTZSPServerConnAddrLookup(CommEvTZSPServer *ev_tzsp_server, struct sockaddr_in *addr);
static CommEvTZSPServerConn *CommEvTZSPServerConnAddrLookupOrNew(CommEvTZSPServer *ev_tzsp_server, struct sockaddr_in *addr);

/* TZSP layer processing PROTOTYPES */
static int CommEvTZSPServerTZSP_EventDispatch(CommEvTZSPServer *ev_tzsp_server, CommEvTZSPPacketParsed *tzsp_packet, int ev_code);
static int CommEvTZSPServerTZSP_PacketProcess(CommEvTZSPServer *ev_tzsp_server, int packet_id, char *packet_ptr, int packet_sz);
static int CommEvTZSPServerTZSP_PacketTAGArrParse(CommEvTZSPServer *ev_tzsp_server, CommEvTZSPPacketParsed *tzsp_packet, int packet_id, char *tzsp_tag_arr_ptr, int tzsp_max_sz);

/* LINK_LAYER DISSECTOR dispatcher */
static int CommEvTZSPServer_LinkLayerDissector_Dispatch(CommEvTZSPServer *ev_tzsp_server, CommEvTZSPPacketParsed *tzsp_packet);
static int CommEvTZSPServer_NetworkLayerDissector_Dispatch(CommEvTZSPServer *ev_tzsp_server, CommEvTZSPPacketParsed *tzsp_packet, CommEvTZSPLinkLayerDissector *ll_dissector, int proto_code);
static int CommEvTZSPServer_TransportLayerDissector_Dispatch(CommEvTZSPServer *ev_tzsp_server, CommEvTZSPPacketParsed *tzsp_packet,	CommEvTZSPNetworkLayerDissector *nl_dissector, int proto_code);

/* Internal LINK LAYER DISSECTORS */
static CommEvTZSPLinkLayerDissectorFunc CommEvTZSPServer_LinkLayerDissector_Ethernet;
static CommEvTZSPLinkLayerDissectorFunc CommEvTZSPServer_LinkLayerDissector_Unknown;

/* Internal NETWORK LAYER DISSECTORS */
static CommEvTZSPNetworkLayerDissectorFunc CommEvTZSPServer_NetworkLayerDissector_IPv4;
static CommEvTZSPNetworkLayerDissectorFunc CommEvTZSPServer_NetworkLayerDissector_IPv6;
static CommEvTZSPNetworkLayerDissectorFunc CommEvTZSPServer_NetworkLayerDissector_VLAN;
static CommEvTZSPNetworkLayerDissectorFunc CommEvTZSPServer_NetworkLayerDissector_Unknown;

/* Internal TRANSPORT LAYER DISSECTORS */
static CommEvTZSPTransportLayerDissectorFunc CommEvTZSPServer_TransportLayerDissector_TCP;
static CommEvTZSPTransportLayerDissectorFunc CommEvTZSPServer_TransportLayerDissector_UDP;
static CommEvTZSPTransportLayerDissectorFunc CommEvTZSPServer_TransportLayerDissector_ICMP;
static CommEvTZSPTransportLayerDissectorFunc CommEvTZSPServer_TransportLayerDissector_Unknown;

/* Internal support functions */
static int CommEvTZSPServerConnStatsCalculate(CommEvTZSPServer *ev_tzsp_server);
static int CommEvTZSPServerConnStatsUpdate(CommEvTZSPServerConn *conn, int data_sz);

/* Internal IO events */
static EvBaseKQCBH CommEvTZSPServerConnTimer;
static EvBaseKQCBH CommEvTZSPServerEventRead;

/**************************************************************************************************************************/
CommEvTZSPServer *CommEvTZSPServerNew(EvKQBase *kq_base)
{
	CommEvTZSPServer *ev_tzsp_server;

	ev_tzsp_server = calloc(1,sizeof(CommEvTZSPServer));

	ev_tzsp_server->kq_base		= kq_base;
	ev_tzsp_server->socket_fd	= -1;

	return ev_tzsp_server;
}
/**************************************************************************************************************************/
int CommEvTZSPServerInit(CommEvTZSPServer *ev_tzsp_server, CommEvTZSPServerConf *ev_tzsp_server_conf)
{
	int op_status;

	/* Sanity check */
	if (!ev_tzsp_server)
		return COMM_TZSP_SERVER_FAILURE_UNKNOWN;

	/* Attach to log base */
	if (ev_tzsp_server_conf->log_base)
		ev_tzsp_server->log_base = ev_tzsp_server_conf->log_base;

	/* Save "listen" port in host order for future reference */
	ev_tzsp_server->port					= ((ev_tzsp_server_conf && ev_tzsp_server_conf->port > 0) ? ev_tzsp_server_conf->port : TZSP_DEFAULT_PORT);

	/* Initialize server_side.addr */
	ev_tzsp_server->addr.sin_family			= AF_INET;
	ev_tzsp_server->addr.sin_port			= htons(ev_tzsp_server->port);
	ev_tzsp_server->addr.sin_addr.s_addr	= htonl(INADDR_ANY);

	/* Create a new UDP non_blocking socket */
	ev_tzsp_server->socket_fd				= EvKQBaseSocketUDPNew(ev_tzsp_server->kq_base);

	if (ev_tzsp_server->socket_fd < 0)
	{
		KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "TZSP_SERVER FAILED BINDING on UDP_PORT [%d] with ERRNO [%d]\n",
				ev_tzsp_server->port, errno);

		return COMM_TZSP_SERVER_FAILURE_SOCKET;
	}

	/* Set socket flags */
	EvKQBaseSocketSetNonBlock(ev_tzsp_server->kq_base, ev_tzsp_server->socket_fd);
	EvKQBaseSocketSetReuseAddr(ev_tzsp_server->kq_base, ev_tzsp_server->socket_fd);
	EvKQBaseSocketSetReusePort(ev_tzsp_server->kq_base, ev_tzsp_server->socket_fd);

	/* Bind UDP socket to port */
	op_status = bind(ev_tzsp_server->socket_fd, (struct sockaddr *)&ev_tzsp_server->addr, sizeof (struct sockaddr_in));

	/* Failed to bind - Close socket and leave */
	if (op_status < 0)
	{
		KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "TZSP_SERVER FAILED BINDING on UDP_PORT [%d] with OP_STATUS [%d]\n",
				ev_tzsp_server->port, op_status);

		EvKQBaseSocketClose(ev_tzsp_server->kq_base, ev_tzsp_server->socket_fd);
		ev_tzsp_server->socket_fd = -1;

		return COMM_TZSP_SERVER_FAILURE_BIND;
	}

	/* Initialize CONN and ACL LIST */
	DLinkedListInit(&ev_tzsp_server->conn.list, BRBDATA_THREAD_UNSAFE);
	DLinkedListInit(&ev_tzsp_server->acl.list, BRBDATA_THREAD_UNSAFE);

	/* Register all protocol DISSECTORs */
	CommEvTZSPServerDissectorRegisterAll(ev_tzsp_server);

	/* Set read internal events for UDP server socket and FD_DESCRIPTION */
	EvKQBaseSetEvent(ev_tzsp_server->kq_base, ev_tzsp_server->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTZSPServerEventRead, ev_tzsp_server);
	EvKQBaseFDDescriptionSetByFD(ev_tzsp_server->kq_base, ev_tzsp_server->socket_fd, "BRB_EV_COMM - TZSP SERVER on PORT [%u]", htons(ev_tzsp_server->port));

	ev_tzsp_server->conn.timer  = -1;

	/* Upper layers asked for DATARATE auto CALC */
	if (ev_tzsp_server_conf->flags.origin_datarate_calc)
	{
		ev_tzsp_server->conn.timer = EvKQBaseTimerAdd(ev_tzsp_server->kq_base, COMM_ACTION_ADD_PERSIST, 1000, CommEvTZSPServerConnTimer, ev_tzsp_server);
		KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_DEBUG, LOGCOLOR_PURPLE, "Datarate CONN timer at TID [%d]\n", ev_tzsp_server->conn.timer);
	}
	else
		KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_DEBUG, LOGCOLOR_PURPLE, "No Datarate CONN timer\n");


	KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "New TZSP_SERVER listening on UDP_PORT [%d]\n", ev_tzsp_server->port);
	return COMM_TZSP_SERVER_INIT_OK;
}
/**************************************************************************************************************************/
void CommEvTZSPServerShutdown(CommEvTZSPServer *ev_tzsp_server)
{
	/* Sanity check */
	if (!ev_tzsp_server)
		return;

	/* Close UDP socket */
	if (ev_tzsp_server->socket_fd > -1)
		EvKQBaseSocketClose(ev_tzsp_server->kq_base, ev_tzsp_server->socket_fd);

	/* Reset FD */
	ev_tzsp_server->socket_fd = -1;
	return;
}
/**************************************************************************************************************************/
void CommEvTZSPServerDestroy(CommEvTZSPServer *ev_tzsp_server)
{
	EvKQBase *ev_base;

	/* Sanity check */
	if (!ev_tzsp_server)
		return;

	/* Grab parent EV_BASE */
	ev_base	= ev_tzsp_server->kq_base;

	/* Delete CONN timer if any */
	if (ev_tzsp_server->conn.timer > 0)
		EvKQBaseTimerCtl(ev_base, ev_tzsp_server->conn.timer, COMM_ACTION_DELETE);

	ev_tzsp_server->conn.timer = -1;

	/* Shutdown and free */
	CommEvTZSPServerShutdown(ev_tzsp_server);
	free(ev_tzsp_server);
	return;
}
/**************************************************************************************************************************/
int CommEvTZSPServerTZSP_EventSet(CommEvTZSPServer *ev_tzsp_server, int ev_code, CommEvTZSPServerCBH *cb_func, void *cb_data)
{
	/* Sanity check */
	if (ev_code >= COMM_TZSP_EVENT_LASTITEM)
		return 0;

	/* Sanity check */
	if (ev_code < 0)
		return 0;

	/* Function handler is mandatory */
	if (!cb_func)
		return 0;

	/* Set event function and callback data */
	ev_tzsp_server->events[ev_code].cb_handler_ptr	= cb_func;
	ev_tzsp_server->events[ev_code].cb_data_ptr		= cb_data;
	ev_tzsp_server->events[ev_code].flags.active	= 1;
	ev_tzsp_server->events[ev_code].flags.enabled	= 1;

	return 1;
}
/**************************************************************************************************************************/
int CommEvTZSPServerTZSP_EventDelete(CommEvTZSPServer *ev_tzsp_server, int ev_code)
{
	/* Sanity check */
	if (ev_code >= COMM_TZSP_EVENT_LASTITEM)
		return 0;

	/* Sanity check */
	if (ev_code < 0)
		return 0;

	/* Set event function and callback data */
	ev_tzsp_server->events[ev_code].cb_handler_ptr	= NULL;
	ev_tzsp_server->events[ev_code].cb_data_ptr		= NULL;
	ev_tzsp_server->events[ev_code].flags.active	= 0;
	ev_tzsp_server->events[ev_code].flags.enabled	= 0;

	return 1;
}
/**************************************************************************************************************************/
int CommEvTZSPServerTZSP_EventDisable(CommEvTZSPServer *ev_tzsp_server, int ev_code)
{
	/* Sanity check */
	if (ev_code >= COMM_TZSP_EVENT_LASTITEM)
		return 0;

	/* Sanity check */
	if (ev_code < 0)
		return 0;

	/* Adjust flags */
	ev_tzsp_server->events[ev_code].flags.active	= 1;
	ev_tzsp_server->events[ev_code].flags.enabled	= 0;

	return 1;
}
/**************************************************************************************************************************/
void CommEvTZSPServerDissectorRegisterAll(CommEvTZSPServer *ev_tzsp_server)
{
	CommEvTZSPLinkLayerDissector *lld_ethernet;
	CommEvTZSPLinkLayerDissector *lld_unknown;
	CommEvTZSPNetworkLayerDissector *nld_vlan;
	CommEvTZSPNetworkLayerDissector *nld_ipv4;
	CommEvTZSPNetworkLayerDissector *nld_ipv6;
	CommEvTZSPNetworkLayerDissector *nld_unknown;
	CommEvTZSPTransportLayerDissector *tld_tcp;
	CommEvTZSPTransportLayerDissector *tld_udp;
	CommEvTZSPTransportLayerDissector *tld_icmp;
	CommEvTZSPTransportLayerDissector *tld_unknown;

	/* Register LINK_LAYER DISSECTORs */
	lld_ethernet	= CommEvTZSPServerLinkLayerDissectorRegister(ev_tzsp_server, CommEvTZSPServer_LinkLayerDissector_Ethernet, COMM_TZSP_ENCAP_ETHERNET, COMM_ETHERNET_FRAME_SIZE);
	lld_unknown		= CommEvTZSPServerLinkLayerDissectorRegister(ev_tzsp_server, CommEvTZSPServer_LinkLayerDissector_Unknown, COMM_TZSP_ENCAP_UNKNOWN, 0);

	/* Special case for VLAN TAG */
	nld_vlan	= CommEvTZSPServerNetworkLayerDissectorRegister(ev_tzsp_server, lld_ethernet, CommEvTZSPServer_NetworkLayerDissector_VLAN, COMM_ETHERNET_ENCAP_VLAN, COMM_ETHVLAN_FRAME_SIZE);

	/* Register NETWORK LAYER DISSECTORs */
	nld_ipv4	= CommEvTZSPServerNetworkLayerDissectorRegister(ev_tzsp_server, lld_ethernet, CommEvTZSPServer_NetworkLayerDissector_IPv4, COMM_ETHERNET_ENCAP_IPV4, COMM_IPV4_HEADER_SIZE);
	nld_ipv6	= CommEvTZSPServerNetworkLayerDissectorRegister(ev_tzsp_server, lld_ethernet, CommEvTZSPServer_NetworkLayerDissector_IPv4, COMM_ETHERNET_ENCAP_IPV6, COMM_IPV6_HEADER_SIZE);

	/* Network DISSECTOR from unknown ETHERNET / NETLAYER and unknown LL / NL */
	nld_unknown	= CommEvTZSPServerNetworkLayerDissectorRegister(ev_tzsp_server, lld_ethernet, CommEvTZSPServer_NetworkLayerDissector_Unknown, COMM_TZSP_ENCAP_UNKNOWN, 0);
	nld_unknown	= CommEvTZSPServerNetworkLayerDissectorRegister(ev_tzsp_server, lld_unknown, CommEvTZSPServer_NetworkLayerDissector_Unknown, COMM_TZSP_ENCAP_UNKNOWN, 0);

	/* Register TRANSPORT LAYER DISSECTORs */
	tld_tcp		= CommEvTZSPServerTransportLayerDissectorRegister(ev_tzsp_server, nld_ipv4, CommEvTZSPServer_TransportLayerDissector_TCP, COMM_IPPROTO_TCP, sizeof(CommEvTCPHeader));
	tld_udp		= CommEvTZSPServerTransportLayerDissectorRegister(ev_tzsp_server, nld_ipv4, CommEvTZSPServer_TransportLayerDissector_UDP, COMM_IPPROTO_UDP, 0);
	tld_icmp	= CommEvTZSPServerTransportLayerDissectorRegister(ev_tzsp_server, nld_ipv4, CommEvTZSPServer_TransportLayerDissector_ICMP, COMM_IPPROTO_ICMP, 0);
	tld_unknown	= CommEvTZSPServerTransportLayerDissectorRegister(ev_tzsp_server, nld_ipv4, CommEvTZSPServer_TransportLayerDissector_Unknown, COMM_TZSP_ENCAP_UNKNOWN, 0);

	return;
}
/**************************************************************************************************************************/
CommEvTZSPLinkLayerDissector *CommEvTZSPServerLinkLayerDissectorRegister(CommEvTZSPServer *ev_tzsp_server, CommEvTZSPLinkLayerDissectorFunc *func, int encap_code, int hdr_size)
{
	CommEvTZSPLinkLayerDissector *ll_dissector;
	char *ll_encap_desc;

	/* Sanity check */
	if (!ev_tzsp_server)
		return NULL;

	/* No more DISSECTORs allowed */
	if (ev_tzsp_server->dissector.count >= (TZSP_LINKLAYER_DISSECTOR_MAX - 1))
	{
		KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Unable to register - Too many registered dissectors [%d]\n",
				ev_tzsp_server->dissector.count);
		return NULL;
	}

	/* Grab humanized description */
	ll_encap_desc				= CommEvTokenStrFromTokenArr((CommEvToken*)&comm_tzsp_encap_desc_arr, encap_code);

	/* Point to current DISSECTOR and populate */
	ll_dissector				= (CommEvTZSPLinkLayerDissector*)&ev_tzsp_server->dissector.arr[ev_tzsp_server->dissector.count++];
	ll_dissector->cb_func		= func;
	ll_dissector->encap_desc	= ll_encap_desc;
	ll_dissector->encap_code	= encap_code;
	ll_dissector->header_size	= hdr_size;
	ll_dissector->id			= (ev_tzsp_server->dissector.count - 1);

	KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "New LINK_LAYER dissector - Encapsulation [%d]-[%s] - LinkLayer header size [%d] bytes\n",
			encap_code, ll_encap_desc, hdr_size);

	return ll_dissector;
}
/**************************************************************************************************************************/
CommEvTZSPNetworkLayerDissector *CommEvTZSPServerNetworkLayerDissectorRegister(CommEvTZSPServer *ev_tzsp_server, CommEvTZSPLinkLayerDissector *ll_dissector,
		CommEvTZSPNetworkLayerDissectorFunc *func,	int proto_code, int proto_hdr_sz)
{
	CommEvTZSPNetworkLayerDissector *nl_dissector;
	char *proto_desc;

	/* Sanity check */
	if (!ll_dissector)
		return NULL;

	/* No more DISSECTORs allowed */
	if (ll_dissector->dissector.count >= (TZSP_NETWORK_DISSECTOR_MAX - 1))
	{
		KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Unable to register - Too many registered protocol dissectors [%d]\n",
				ll_dissector->dissector.count);
		return NULL;
	}

	/* Grab humanized description */
	proto_desc				= CommEvTokenStrFromTokenArr((CommEvToken*)&comm_tzsp_ethertype_desc_arr, proto_code);

	/* Point to current DISSECTOR and populate */
	nl_dissector					= (CommEvTZSPNetworkLayerDissector*)&ll_dissector->dissector.arr[ll_dissector->dissector.count++];

	nl_dissector->ll_dissector		= ll_dissector;
	nl_dissector->ll_dissector_id	= ll_dissector->id;
	nl_dissector->id				= (ll_dissector->dissector.count - 1);
	nl_dissector->cb_func			= func;
	nl_dissector->proto_desc		= proto_desc;
	nl_dissector->proto_code		= proto_code;
	nl_dissector->header_size		= proto_hdr_sz;

	KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "New NETWORK_LAYER dissector - LL [%d]-[%s] - NL [%d]-[%s] - HDR_SIZE [%d]\n",
			ll_dissector->encap_code, ll_dissector->encap_desc, proto_code, proto_desc, proto_hdr_sz);

	return nl_dissector;
}
/**************************************************************************************************************************/
CommEvTZSPTransportLayerDissector *CommEvTZSPServerTransportLayerDissectorRegister(CommEvTZSPServer *ev_tzsp_server, CommEvTZSPNetworkLayerDissector *nl_dissector,
		CommEvTZSPTransportLayerDissectorFunc *func, int proto_code, int proto_hdr_sz)
{
	CommEvTZSPTransportLayerDissector *tl_dissector;
	char *proto_desc;

	/* Sanity check */
	if (!nl_dissector)
		return NULL;

	/* No more DISSECTORs allowed */
	if (nl_dissector->dissector.count >= (TZSP_TRANSPORT_DISSECTOR_MAX - 1))
	{
		KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Unable to register - Too many registered protocol dissectors [%d]\n",
				nl_dissector->dissector.count);
		return NULL;
	}

	/* Grab humanized description */
	proto_desc				= CommEvTokenStrFromTokenArr((CommEvToken*)&comm_tzsp_ipproto_desc_arr, proto_code);

	/* Point to current DISSECTOR and populate */
	tl_dissector					= (CommEvTZSPTransportLayerDissector*)&nl_dissector->dissector.arr[nl_dissector->dissector.count++];
	tl_dissector->nl_dissector_id	= nl_dissector->id;
	tl_dissector->id				= (nl_dissector->dissector.count - 1);
	tl_dissector->cb_func			= func;
	tl_dissector->proto_desc		= proto_desc;
	tl_dissector->proto_code		= proto_code;
	tl_dissector->header_size		= proto_hdr_sz;

	KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "New TRANSPORT_LAYER dissector - NL [%d]-[%s] - TL [%d]-[%s] - HDR_SIZE [%d]\n",
			nl_dissector->proto_code, nl_dissector->proto_desc, proto_code, proto_desc, proto_hdr_sz);

	return tl_dissector;
}
/**************************************************************************************************************************/
CommEvTZSPLinkLayerDissector *CommEvTZSPServerLinkLayerDissectorByEncap(CommEvTZSPServer *ev_tzsp_server, int encap_code)
{
	CommEvTZSPLinkLayerDissector *ll_dissector;
	int i;

	for (i = 0; i < ev_tzsp_server->dissector.count; i++)
	{
		ll_dissector = (CommEvTZSPLinkLayerDissector*)&ev_tzsp_server->dissector.arr[i];

		/* Found matching PROTO, dispatch to LINKLAYER_DISSECTOR */
		if (encap_code == ll_dissector->encap_code)
			return ll_dissector;
	}

	return NULL;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static CommEvTZSPServerACL *CommEvTZSPServerACLNew(CommEvTZSPServer *ev_tzsp_server, struct sockaddr_in addr)
{
	CommEvTZSPServerACL *acl;
	EvKQBase *kq_base = ev_tzsp_server->kq_base;

	/* Create a new connection */
	acl = calloc(1, sizeof(CommEvTZSPServerACL));
	acl->parent = ev_tzsp_server;

	/* Save data into ACL */
	memcpy(&acl->addr, &addr, sizeof(struct sockaddr_in));

	/* ADD into linked list */
	DLinkedListAdd(&ev_tzsp_server->acl.list, &acl->node, acl);
	return acl;
}
/**************************************************************************************************************************/
static int CommEvTZSPServerACLDestroy(CommEvTZSPServerACL *acl)
{
	CommEvTZSPServer *ev_tzsp_server = acl->parent;

	/* Remove from list and destroy */
	DLinkedListDelete(&ev_tzsp_server->acl.list, &acl->node);
	free(acl);

	return 1;
}
/**************************************************************************************************************************/
static CommEvTZSPServerACL *CommEvTZSPServerACLAddrAdd(CommEvTZSPServer *ev_tzsp_server, struct sockaddr_in addr)
{
	CommEvTZSPServerACL *acl;

	/* Try to find ACL */
	acl = CommEvTZSPServerACLAddrFind(ev_tzsp_server, addr);

	/* Not found, create a new one */
	if (!acl)
		CommEvTZSPServerACLNew(ev_tzsp_server, addr);

	return acl;
}
/**************************************************************************************************************************/
static CommEvTZSPServerACL *CommEvTZSPServerACLAddrFind(CommEvTZSPServer *ev_tzsp_server, struct sockaddr_in addr)
{
	DLinkedListNode *node;
	CommEvTZSPServerACL *acl = NULL;

	/* Walk the list searching for node */
	for (node = ev_tzsp_server->acl.list.head; node; node = node->next)
	{
		acl = node->data;

		/* Found it, return */
		if (!memcmp(&acl->addr.sin_addr, &addr.sin_addr, sizeof(struct in_addr)))
			return acl;

		continue;
	}

	return NULL;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static CommEvTZSPServerConn *CommEvTZSPServerConnNew(CommEvTZSPServer *ev_tzsp_server, struct sockaddr_in *addr)
{
	CommEvTZSPServerConn *conn;
	EvKQBase *kq_base = ev_tzsp_server->kq_base;

	/* Create a new connection */
	conn = calloc(1, sizeof(CommEvTZSPServerConn));
	conn->parent = ev_tzsp_server;

	/* Save data into CONN */
	memcpy(&conn->addr, addr, sizeof(struct sockaddr_in));
	memcpy(&conn->time.add_tv, &kq_base->stats.cur_invoke_tv, sizeof(struct timeval));
	conn->time.add_ts = kq_base->stats.cur_invoke_ts_sec;

	/* Print string address */
	inet_ntop(AF_INET, &addr->sin_addr, (char*)&conn->addr_str, INET_ADDRSTRLEN);

	/* Insert into linked list */
	DLinkedListAdd(&ev_tzsp_server->conn.list, &conn->node, conn);
	return conn;
}
/**************************************************************************************************************************/
static int CommEvTZSPServerConnDestroy(CommEvTZSPServerConn *conn)
{
	CommEvTZSPServer *ev_tzsp_server = conn->parent;

	/* Remove from list and destroy */
	DLinkedListDelete(&ev_tzsp_server->conn.list, &conn->node);
	free(conn);

	return 1;
}
/**************************************************************************************************************************/
static CommEvTZSPServerConn *CommEvTZSPServerConnAddrLookup(CommEvTZSPServer *ev_tzsp_server, struct sockaddr_in *addr)
{
	DLinkedListNode *node;
	CommEvTZSPServerConn *conn = NULL;

	/* Walk the list searching for node */
	for (node = ev_tzsp_server->conn.list.head; node; node = node->next)
	{
		conn = node->data;

		/* Found it, return */
		if (!memcmp(&conn->addr.sin_addr, &addr->sin_addr, sizeof(struct in_addr)))
			return conn;

		continue;
	}

	return NULL;
}
/**************************************************************************************************************************/
static CommEvTZSPServerConn *CommEvTZSPServerConnAddrLookupOrNew(CommEvTZSPServer *ev_tzsp_server, struct sockaddr_in *addr)
{
	CommEvTZSPServerConn *conn = CommEvTZSPServerConnAddrLookup(ev_tzsp_server, addr);

	/* Unable to find, create a new one */
	if (!conn)
		conn = CommEvTZSPServerConnNew(ev_tzsp_server, addr);

	return conn;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvTZSPServerTZSP_EventDispatch(CommEvTZSPServer *ev_tzsp_server, CommEvTZSPPacketParsed *tzsp_packet, int ev_code)
{
	int ev_ret;

	/* Sanity check */
	if (ev_code >= COMM_TZSP_EVENT_LASTITEM)
		return 0;

	/* Dispatch user supplied callback for packet processing */
	if ((ev_tzsp_server->events[ev_code].flags.active) && (ev_tzsp_server->events[ev_code].flags.enabled))
	{
		if (!ev_tzsp_server->events[ev_code].cb_handler_ptr)
		{
			KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "EV_CODE [%d] - Empty callback handler for active event\n", ev_code);
			return 0;
		}

		/* Dispatch event */
		ev_ret = ev_tzsp_server->events[ev_code].cb_handler_ptr(ev_tzsp_server, ev_tzsp_server->events[ev_code].cb_data_ptr, tzsp_packet, ev_code);
		return ev_ret;
	}

	return 0;
}
/**************************************************************************************************************************/
static int CommEvTZSPServerTZSP_PacketProcess(CommEvTZSPServer *ev_tzsp_server, int packet_id, char *packet_ptr, int packet_sz)
{
	CommEvTZSPPacketParsed tzsp_packet;
	int tzsp_tag_arr_sz;
	char *tzsp_payload_ptr;

	CommEvTZSPHeader *tzsp_header	= (CommEvTZSPHeader*)packet_ptr;
	char *tzsp_tag_arr_ptr			= (packet_ptr + sizeof(CommEvTZSPHeader));
	char *tzsp_encap_desc			= CommEvTokenStrFromTokenArr((CommEvToken*)&comm_tzsp_encap_desc_arr, ntohs(tzsp_header->protocol));
	int tzsp_payload_sz				= (packet_sz - sizeof(CommEvTZSPHeader));

	/* Sanity check */
	if (packet_sz <= sizeof(CommEvTZSPHeader))
	{
		KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "PACKET_SZ [%d] - Too small\n", packet_sz);
		return 0;
	}

	/* Clean up stack */
	memset(&tzsp_packet, 0, sizeof(CommEvTZSPPacketParsed));

	/* Parse TAG array and calculate TAG_ARR size */
	tzsp_tag_arr_sz	= CommEvTZSPServerTZSP_PacketTAGArrParse(ev_tzsp_server, &tzsp_packet, packet_id, tzsp_tag_arr_ptr, tzsp_payload_sz);

	/* Point to packet PAYLOAD and adjust PAYLOAD_SZ */
	tzsp_payload_ptr = (tzsp_tag_arr_ptr + tzsp_tag_arr_sz);
	tzsp_payload_sz	-= tzsp_tag_arr_sz;

	KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "PACKET_ID [%d] - SIZE [%d] - TZSP VERSION [%u] - TYPE [%u] - PROTO [%u]-[%s] - "
			"TAG_ARR_SZ [%d] - PAYLOAD_SZ [%d]\n", packet_id, packet_sz, tzsp_header->version, tzsp_header->type, ntohs(tzsp_header->protocol),
			tzsp_encap_desc, tzsp_tag_arr_sz, tzsp_payload_sz);

	/* Begin filling our TZSP_PACKET info */
	tzsp_packet.packet_id			= packet_id;
	tzsp_packet.tzsp.proto.id		= ntohs(tzsp_header->protocol);
	tzsp_packet.tzsp.proto.desc_str	= tzsp_encap_desc;

	/* Begin filling PAYLOAD (link layer) info */
	tzsp_packet.payload.size_total		= tzsp_payload_sz;
	tzsp_packet.payload.link_layer.ptr	= tzsp_payload_ptr;

	/* Send packet to LINK LAYER DISSECTOR */
	CommEvTZSPServer_LinkLayerDissector_Dispatch(ev_tzsp_server, &tzsp_packet);

	/* Dispatch upper layer events */
	CommEvTZSPServerTZSP_EventDispatch(ev_tzsp_server, &tzsp_packet, COMM_TZSP_EVENT_PACKET);
	return 1;
}
/**************************************************************************************************************************/
static int CommEvTZSPServerTZSP_PacketTAGArrParse(CommEvTZSPServer *ev_tzsp_server, CommEvTZSPPacketParsed *tzsp_packet, int packet_id, char *tzsp_tag_arr_ptr, int tzsp_max_sz)
{
	char *tag_desc_str;

	unsigned char tag_cur_opcode	= 0;
	unsigned char tag_cur_size		= 0;
	unsigned char tag_cur_uc_value	= 0;
	int tag_cur_offset				= 0;
	int tag_count					= 0;
	int tag_end_found				= 0;

	/* Sanity check */
	if (tzsp_max_sz <= 0)
		return 0;

	while (1)
	{
		/* Sanity check */
		if (tag_cur_offset >= tzsp_max_sz)
			break;

		/* Read TAG current CODE and SIZE */
		tag_cur_opcode	= tzsp_tag_arr_ptr[tag_cur_offset];
		tag_cur_size	= tzsp_tag_arr_ptr[tag_cur_offset + 1];
		tag_desc_str	= CommEvTokenStrFromTokenArr((CommEvToken*)&comm_tzsp_tag_desc_arr, tag_cur_opcode);

		KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "PACKET_ID [%d] - CUR_OFF [%d] - TAG [%u / %s] - TAG_SZ [%u]\n",
				packet_id, tag_cur_offset, tag_cur_opcode, tag_desc_str, tag_cur_size);

		/* Process TZSP OPCODE */
		switch(tag_cur_opcode)
		{

		/* Ignore padding */
		case COMM_TZSP_TAG_PADDING:
		{
			tag_cur_offset++;
			continue;
		}

		/* TAG finished */
		case COMM_TZSP_TAG_END:
		{
			//KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "PACKET_ID [%d] - END TAG found, STOP\n", packet_id);

			/* Mark as END found and STOP */
			tag_cur_offset++;
			tag_end_found = 1;
			break;
		}

		case COMM_TZSP_TAG_RAW_RSSI:
		{
			/* Grab RSSI value */
			tag_cur_uc_value = tzsp_tag_arr_ptr[tag_cur_offset + 2];
			KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "PACKET_ID [%d] - RAW RSSI [%u]\n", packet_id, ntohs(tag_cur_uc_value));
			break;
		}

		case COMM_TZSP_TAG_SNR:
		{
			KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "PACKET_ID [%d] - SNR\n", packet_id);
			break;
		}

		case COMM_TZSP_TAG_DATA_RATE:
		{
			KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "PACKET_ID [%d] - DATA RATE\n", packet_id);
			break;
		}

		case COMM_TZSP_TAG_TIMESTAMP:
		{
			KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "PACKET_ID [%d] - TIMESTAMP\n", packet_id);
			break;
		}

		case COMM_TZSP_TAG_CONTENTION_FREE:
		{
			KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "PACKET_ID [%d] - CONTENTION FREE\n", packet_id);
			break;
		}

		case COMM_TZSP_TAG_DECRYPT_FAIL:
		{
			KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "PACKET_ID [%d] - DECRYPT FAIL\n", packet_id);
			break;
		}

		case COMM_TZSP_TAG_FCS_ERROR:
		{
			KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "PACKET_ID [%d] - FCS ERROR\n", packet_id);
			break;
		}

		case COMM_TZSP_TAG_RX_CHANNEL:
		{
			KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "PACKET_ID [%d] - RX CHANNEL\n", packet_id);
			break;
		}

		case COMM_TZSP_TAG_PACKET_COUNT:
		{
			KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "PACKET_ID [%d] - PACKET COUNT\n", packet_id);
			break;
		}

		case COMM_TZSP_TAG_FRAME_LENGTH:
		{
			KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "PACKET_ID [%d] - FRAME LENGTH\n", packet_id);
			break;
		}

		case COMM_TZSP_TAG_RADIO_HDR_SERIAL:
		{
			KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "PACKET_ID [%d] - RADIO HDR SERIAL\n", packet_id);
			break;
		}

		default:
		{
			KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "PACKET_ID [%d] - UNKNOWN TAG [%02X]\n", packet_id, tag_cur_opcode);
			break;
		}
		} //switch(tag_cur_opcode)

		/* Break outer loop or move to next offset */
		if (tag_end_found)
			break;
		/* Increment CUR_SZ + 1 to bypass OPCODE itself */
		else
			tag_cur_offset += tag_cur_size;

		continue;
	}

	return tag_cur_offset;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvTZSPServer_LinkLayerDissector_Dispatch(CommEvTZSPServer *ev_tzsp_server, CommEvTZSPPacketParsed *tzsp_packet)
{
	CommEvTZSPLinkLayerDissector *ll_dissector;
	int nl_proto;
	int i;

	CommEvTZSPLinkLayerDissector *ll_dissector_unknown	= NULL;
	int match_count										= 0;

	/* Search for an available DISSECTOR for this PROTOCOL */
	for (i = 0; i < ev_tzsp_server->dissector.count; i++)
	{
		ll_dissector = (CommEvTZSPLinkLayerDissector*)&ev_tzsp_server->dissector.arr[i];

		/* Unknown DISSECTOR found, save it */
		if (COMM_TZSP_ENCAP_UNKNOWN == ll_dissector->encap_code)
		{
			ll_dissector_unknown = ll_dissector;
			continue;
		}

		assert(ll_dissector->header_size > 0);

		/* Found matching PROTO, dispatch to LINKLAYER_DISSECTOR */
		if (tzsp_packet->tzsp.proto.id != ll_dissector->encap_code)
			continue;

		KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "PACKET_ID [%d] - MATCH_COUNT [%d] - LinkLayer detected [%d]-[%s]\n",
				tzsp_packet->packet_id, match_count, ll_dissector->encap_code, ll_dissector->encap_desc);

		/* Adjust TZSP packet for this LINKLAYER and touch MATCH_COUNT */
		tzsp_packet->payload.link_layer.size	= ll_dissector->header_size;
		tzsp_packet->payload.network_layer.ptr	= (tzsp_packet->payload.link_layer.ptr + tzsp_packet->payload.link_layer.size);
		match_count++;

		/* Invoke LINKLAYER DISSECTOR function - It will return NETWORK LAYER protocol code for further examination */
		nl_proto	= ll_dissector->cb_func(ev_tzsp_server, tzsp_packet, ll_dissector, 0);

		/* Dispatch to NETWORK LAYER */
		CommEvTZSPServer_NetworkLayerDissector_Dispatch(ev_tzsp_server, tzsp_packet, ll_dissector, nl_proto);
		break;
	}

	/* None found, invoke UNKNOWN PROTO DISSECTOR */
	if (0 == match_count)
	{
		KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "PACKET_ID [%d] - LinkLayer unknown [%d] - Will invoke UNKNOWN DISSECTOR [%p]\n",
				tzsp_packet->packet_id, tzsp_packet->tzsp.proto.id, ll_dissector_unknown);

		/* No LL DISSECTOR for unknown PROTO */
		if (!ll_dissector_unknown)
			return 0;

		/* Adjust TZSP packet for this LINKLAYER */
		tzsp_packet->payload.link_layer.size	= ll_dissector_unknown->header_size;
		tzsp_packet->payload.network_layer.ptr	= (tzsp_packet->payload.link_layer.ptr + tzsp_packet->payload.link_layer.size);

		/* Invoke UNKNOWN LINKLAYER DISSECTOR function */
		nl_proto = ll_dissector_unknown->cb_func(ev_tzsp_server, tzsp_packet, ll_dissector_unknown, 0);

		/* Dispatch to NETWORK LAYER */
		CommEvTZSPServer_NetworkLayerDissector_Dispatch(ev_tzsp_server, tzsp_packet, ll_dissector_unknown, nl_proto);
		return 0;
	}

	return match_count;
}
/**************************************************************************************************************************/
static int CommEvTZSPServer_NetworkLayerDissector_Dispatch(CommEvTZSPServer *ev_tzsp_server, CommEvTZSPPacketParsed *tzsp_packet,
		CommEvTZSPLinkLayerDissector *ll_dissector, int proto_code)
{
	CommEvTZSPNetworkLayerDissector *nl_dissector;
	int tl_proto;
	int i;

	CommEvTZSPNetworkLayerDissector *nl_dissector_unknown	= NULL;
	int match_count											= 0;

	KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "PACKET_ID [%d] - NET_PROTO [0x%04X]\n", tzsp_packet->packet_id, proto_code);

	/* Search for an available DISSECTOR for this NETWORK_LAYER inside this LINK_LAYER */
	for (i = 0; i < ll_dissector->dissector.count; i++)
	{
		nl_dissector = (CommEvTZSPNetworkLayerDissector*)&ll_dissector->dissector.arr[i];

		/* Unknown DISSECTOR found, save it */
		if (COMM_TZSP_ENCAP_UNKNOWN == nl_dissector->proto_code)
		{
			nl_dissector_unknown = nl_dissector;
			continue;
		}

		assert(ll_dissector->header_size > 0);

		/* Found matching PROTO, dispatch to NETWORK_DISSECTOR */
		if (proto_code != nl_dissector->proto_code)
			continue;

		KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "PACKET_ID [%d] - MATCH_COUNT [%d] - NetworkLayer detected [%d]-[%s]\n",
				tzsp_packet->packet_id, match_count, nl_dissector->proto_code, nl_dissector->proto_desc);

		/* Adjust TZSP packet for this NETWORK LAYER and touch MATCH_COUNT */
		tzsp_packet->payload.network_layer.size		= nl_dissector->header_size;
		tzsp_packet->payload.transport_layer.ptr	= (tzsp_packet->payload.network_layer.ptr + tzsp_packet->payload.network_layer.size);
		match_count++;

		/* Invoke NETWORK LAYER DISSECTOR function - It will return TRANSPORT LAYER protocol code for further examination */
		tl_proto	= nl_dissector->cb_func(ev_tzsp_server, tzsp_packet, nl_dissector, proto_code);

		/* Dispatch to TRANSPORT LAYER */
		CommEvTZSPServer_TransportLayerDissector_Dispatch(ev_tzsp_server, tzsp_packet, nl_dissector, tl_proto);

		break;
	}

	/* None found, invoke UNKNOWN PROTO DISSECTOR */
	if (0 == match_count)
	{
		KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "PACKET_ID [%d] - NetworkLayer unknown [0x%04X] - Will invoke UNKNOWN DISSECTOR [%p]\n",
				tzsp_packet->packet_id, proto_code, nl_dissector_unknown);

		/* No NL DISSECTOR for unknown PROTO */
		if (!nl_dissector_unknown)
			return 0;

		/* Adjust TZSP packet for this LINKLAYER */
		tzsp_packet->payload.network_layer.size		= nl_dissector_unknown->header_size;
		tzsp_packet->payload.transport_layer.ptr	= (tzsp_packet->payload.network_layer.ptr + tzsp_packet->payload.network_layer.size);

		/* Invoke UNKNOWN LINKLAYER DISSECTOR function */
		tl_proto = nl_dissector_unknown->cb_func(ev_tzsp_server, tzsp_packet, nl_dissector_unknown, proto_code);

		/* Dispatch to TRANSPORT LAYER */
		CommEvTZSPServer_TransportLayerDissector_Dispatch(ev_tzsp_server, tzsp_packet, nl_dissector_unknown, tl_proto);
		return 0;
	}


	return match_count;
}
/**************************************************************************************************************************/
static int CommEvTZSPServer_TransportLayerDissector_Dispatch(CommEvTZSPServer *ev_tzsp_server, CommEvTZSPPacketParsed *tzsp_packet,
		CommEvTZSPNetworkLayerDissector *nl_dissector, int proto_code)
{
	CommEvTZSPTransportLayerDissector *tl_dissector;
	int i;

	CommEvTZSPTransportLayerDissector *tl_dissector_unknown	= NULL;
	int match_count											= 0;

	/* Search for an available DISSECTOR for this TRANSPORT_LAYER inside this NETWORK_LAYER */
	for (i = 0; i < nl_dissector->dissector.count; i++)
	{
		tl_dissector = (CommEvTZSPTransportLayerDissector*)&nl_dissector->dissector.arr[i];

		/* Unknown DISSECTOR found, save it */
		if (COMM_TZSP_ENCAP_UNKNOWN == nl_dissector->proto_code)
		{
			tl_dissector_unknown = tl_dissector;
			continue;
		}

		/* Found matching PROTO, dispatch to TRANSPORT_LAYER_DISSECTOR */
		if (proto_code != tl_dissector->proto_code)
			continue;


		KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "PACKET_ID [%d] - MATCH_COUNT [%d] - TransportLayer detected [%d]-[%s]\n",
				tzsp_packet->packet_id, match_count, tl_dissector->proto_code, tl_dissector->proto_desc);

		/* Adjust TZSP packet for this NETWORK LAYER and touch MATCH_COUNT */
		tzsp_packet->payload.transport_layer.size	= tl_dissector->header_size;
		match_count++;

		/* Invoke TRANSPORT LAYER DISSECTOR function  */
		tl_dissector->cb_func(ev_tzsp_server, tzsp_packet, nl_dissector, proto_code);
		break;
	}

	/* None found, invoke UNKNOWN PROTO DISSECTOR */
	if (0 == match_count)
	{
		KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_WARNING, LOGCOLOR_PURPLE, "PACKET_ID [%d] - Transport Layer unknown [%d] - Will invoke UNKNOWN DISSECTOR [%p]\n",
				tzsp_packet->packet_id, proto_code, tl_dissector_unknown);

		/* No TL DISSECTOR for unknown PROTO */
		if (!tl_dissector_unknown)
			return 0;

		/* Adjust TZSP packet for this LINKLAYER */
		tzsp_packet->payload.transport_layer.size		= tl_dissector_unknown->header_size;

		/* Invoke UNKNOWN TRANSPORT LAYER DISSECTOR function */
		tl_dissector_unknown->cb_func(ev_tzsp_server, tzsp_packet, tl_dissector_unknown, proto_code);
		return 0;
	}

	return match_count;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvTZSPServer_LinkLayerDissector_Ethernet(void *ev_tzsp_server_ptr, void *tzsp_packet_ptr, void *tzsp_ll_dissector_ptr, int proto_code)
{
	CommEvTZSPServer *ev_tzsp_server			= ev_tzsp_server_ptr;
	CommEvTZSPPacketParsed *tzsp_packet			= tzsp_packet_ptr;
	CommEvTZSPLinkLayerDissector *ll_dissector	= tzsp_ll_dissector_ptr;
	CommEvEthernetHeader *ether_hdr				= (CommEvEthernetHeader*)tzsp_packet->payload.link_layer.ptr;
	unsigned int nl_proto_code					= ntohs(ether_hdr->type);

	/* Create DESTINATION MAC string */
	snprintf((char*)&tzsp_packet->parsed_data.link_layer.ethernet.mac_dst_str, sizeof(tzsp_packet->parsed_data.link_layer.ethernet.mac_dst_str),
			"%02X:%02X:%02X:%02X:%02X:%02X", ether_hdr->mac_dst[0], ether_hdr->mac_dst[1], ether_hdr->mac_dst[2], ether_hdr->mac_dst[3],
			ether_hdr->mac_dst[4], ether_hdr->mac_dst[5]);

	/* Create SOURCE MAC string */
	snprintf((char*)&tzsp_packet->parsed_data.link_layer.ethernet.mac_src_str, sizeof(tzsp_packet->parsed_data.link_layer.ethernet.mac_src_str),
			"%02X:%02X:%02X:%02X:%02X:%02X", ether_hdr->mac_src[0], ether_hdr->mac_src[1], ether_hdr->mac_src[2], ether_hdr->mac_src[3],
			ether_hdr->mac_src[4], ether_hdr->mac_src[5]);

	/* Reset VLAN ID */
	tzsp_packet->parsed_data.network_layer.vlan.id = -1;

	KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "PACKET_ID [%d] - MAC SRC/DST [%s]-[%s] - NL_PROTO [0x%04X]\n",
			tzsp_packet->packet_id, tzsp_packet->parsed_data.link_layer.ethernet.mac_src_str, tzsp_packet->parsed_data.link_layer.ethernet.mac_dst_str, nl_proto_code);

	//EvKQBaseLoggerHexDump(ev_tzsp_server->log_base, LOGTYPE_CRITICAL, tzsp_packet->payload.proto_ptr, 32, 8, 4);
	return nl_proto_code;
}
/**************************************************************************************************************************/
static int CommEvTZSPServer_LinkLayerDissector_Unknown(void *ev_tzsp_server_ptr, void *tzsp_packet_ptr, void *tzsp_ll_dissector_ptr, int proto_code)
{
	CommEvTZSPServer *ev_tzsp_server			= ev_tzsp_server_ptr;
	CommEvTZSPPacketParsed *tzsp_packet			= tzsp_packet_ptr;
	CommEvTZSPLinkLayerDissector *ll_dissector	= tzsp_ll_dissector_ptr;

	KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "PACKET_ID [%d] - Unknown Link Layer [%04X]\n", tzsp_packet->packet_id, proto_code);

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvTZSPServer_NetworkLayerDissector_IPv4(void *ev_tzsp_server_ptr, void *tzsp_packet_ptr, void *tzsp_nl_dissector_ptr, int proto_code)
{
	CommEvTZSPServer *ev_tzsp_server				= ev_tzsp_server_ptr;
	CommEvTZSPPacketParsed *tzsp_packet				= tzsp_packet_ptr;
	CommEvTZSPNetworkLayerDissector *nl_dissector	= tzsp_nl_dissector_ptr;
	CommEvIPHeader *ip_hdr							= (CommEvIPHeader*)tzsp_packet->payload.network_layer.ptr;
	unsigned int sock_sz							= sizeof(struct in_addr);
	int ip_hdr_sz									= ((ip_hdr->ip_vhl & 0xF) << 2);

	/* Convert IP to string */
	inet_ntop(AF_INET, &ip_hdr->source_ip, (char*)&tzsp_packet->parsed_data.network_layer.ipv4.src_str, INET_ADDRSTRLEN);
	inet_ntop(AF_INET, &ip_hdr->dest_ip, (char*)&tzsp_packet->parsed_data.network_layer.ipv4.dst_str, INET_ADDRSTRLEN);

	/* Save IP_PROTOCOL code and IP_PAYLOAD size */
	tzsp_packet->parsed_data.network_layer.ipv4.proto_id	= ip_hdr->proto;
	tzsp_packet->parsed_data.network_layer.ipv4.payload_sz	= ntohs(ip_hdr->total_len);
	tzsp_packet->parsed_data.network_layer.flags.ipv4		= 1;

	KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "PACKET_ID [%d] - PROTO [%u] - SRC/DST IP [%s]-[%s] - DATA_SZ [%u]\n",
			tzsp_packet->packet_id, ip_hdr->proto, tzsp_packet->parsed_data.network_layer.ipv4.src_str, tzsp_packet->parsed_data.network_layer.ipv4.dst_str,
			ntohs(ip_hdr->total_len));

	return ip_hdr->proto;
}
/**************************************************************************************************************************/
static int CommEvTZSPServer_NetworkLayerDissector_IPv6(void *ev_tzsp_server_ptr, void *tzsp_packet_ptr, void *tzsp_nl_dissector_ptr, int proto_code)
{
	CommEvTZSPServer *ev_tzsp_server				= ev_tzsp_server_ptr;
	CommEvTZSPPacketParsed *tzsp_packet				= tzsp_packet_ptr;
	CommEvTZSPNetworkLayerDissector *nl_dissector	= tzsp_nl_dissector_ptr;

	KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "PACKET_ID [%d] - IPv6 packet\n", tzsp_packet->packet_id);

	tzsp_packet->parsed_data.network_layer.flags.ipv6		= 1;
	return 1;
}
/**************************************************************************************************************************/
static int CommEvTZSPServer_NetworkLayerDissector_VLAN(void *ev_tzsp_server_ptr, void *tzsp_packet_ptr, void *tzsp_nl_dissector_ptr, int proto_code)
{
	CommEvTZSPServer *ev_tzsp_server				= ev_tzsp_server_ptr;
	CommEvTZSPPacketParsed *tzsp_packet				= tzsp_packet_ptr;
	CommEvTZSPNetworkLayerDissector *nl_dissector	= tzsp_nl_dissector_ptr;
	CommEvEthernetHeader *ether_hdr					= (CommEvEthernetHeader*)tzsp_packet->payload.link_layer.ptr;
	struct ether_vlan_header *vlan_hdr 				= (struct ether_vlan_header *) tzsp_packet->payload.link_layer.ptr;

	unsigned int nl_proto_code						= ntohs(vlan_hdr->evl_proto);
	unsigned int vlan_id 							= ntohs(vlan_hdr->evl_tag);

	/* Save IP_PROTOCOL code and IP_PAYLOAD size */
	tzsp_packet->parsed_data.network_layer.vlan.id		= vlan_id;
	tzsp_packet->parsed_data.network_layer.flags.vlan	= 1;

	KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "PACKET_ID [%d] - MAC SRC/DST [%s]-[%s] - VLAN_ID [%u]\n",
			tzsp_packet->packet_id, tzsp_packet->parsed_data.link_layer.ethernet.mac_src_str, tzsp_packet->parsed_data.link_layer.ethernet.mac_dst_str, vlan_id);

	/* Adjust TZSP packet for this LINKLAYER and touch MATCH_COUNT */
	tzsp_packet->payload.link_layer.size	= nl_dissector->ll_dissector->header_size;
	tzsp_packet->payload.network_layer.ptr	= (tzsp_packet->payload.link_layer.ptr + tzsp_packet->payload.link_layer.size) + 4;

	/* Dispatch to NETWORK LAYER */
	CommEvTZSPServer_NetworkLayerDissector_Dispatch(ev_tzsp_server, tzsp_packet, nl_dissector->ll_dissector, nl_proto_code);

	return vlan_id;
}
/**************************************************************************************************************************/
static int CommEvTZSPServer_NetworkLayerDissector_Unknown(void *ev_tzsp_server_ptr, void *tzsp_packet_ptr, void *tzsp_nl_dissector_ptr, int proto_code)
{
	CommEvTZSPServer *ev_tzsp_server				= ev_tzsp_server_ptr;
	CommEvTZSPPacketParsed *tzsp_packet				= tzsp_packet_ptr;
	CommEvTZSPNetworkLayerDissector *nl_dissector	= tzsp_nl_dissector_ptr;

	KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "PACKET_ID [%d] - Uknown Network Layer [%04X]\n", tzsp_packet->packet_id, proto_code);
	tzsp_packet->parsed_data.network_layer.flags.unknown	= 1;

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvTZSPServer_TransportLayerDissector_TCP(void *ev_tzsp_server_ptr, void *tzsp_packet_ptr, void *tzsp_tl_dissector_ptr, int proto_code)
{
	CommEvTZSPServer *ev_tzsp_server				= ev_tzsp_server_ptr;
	CommEvTZSPPacketParsed *tzsp_packet				= tzsp_packet_ptr;
	CommEvTZSPTransportLayerDissector *tl_dissector	= tzsp_tl_dissector_ptr;
	CommEvIPHeader *ip_hdr							= (CommEvIPHeader*)tzsp_packet->payload.network_layer.ptr;
	CommEvTCPHeader *tcp_hdr						= (CommEvTCPHeader*)tzsp_packet->payload.transport_layer.ptr;
	unsigned int tcp_flags							= ntohs(*(unsigned short *)(tzsp_packet->payload.transport_layer.ptr + 12)) & 0x0FFF;

	/* Set parsed packet flags as TCP */
	tzsp_packet->parsed_data.transport_layer.flags.tcp = 1;

	/* Parse TCP flags from bitmap */
	if ((tcp_flags & COMM_TCPFLAG_FIN) != 0)
		tzsp_packet->parsed_data.transport_layer.tcp.flags.fin = 1;
	if ((tcp_flags & COMM_TCPFLAG_SYN) != 0)
		tzsp_packet->parsed_data.transport_layer.tcp.flags.syn = 1;
	if ((tcp_flags & COMM_TCPFLAG_RST) != 0)
		tzsp_packet->parsed_data.transport_layer.tcp.flags.rst = 1;
	if ((tcp_flags & COMM_TCPFLAG_PSH) != 0)
		tzsp_packet->parsed_data.transport_layer.tcp.flags.psh = 1;
	if ((tcp_flags & COMM_TCPFLAG_ACK) != 0)
		tzsp_packet->parsed_data.transport_layer.tcp.flags.ack = 1;
	if ((tcp_flags & COMM_TCPFLAG_URG) != 0)
		tzsp_packet->parsed_data.transport_layer.tcp.flags.urg = 1;
	if ((tcp_flags & COMM_TCPFLAG_ECN) != 0)
		tzsp_packet->parsed_data.transport_layer.tcp.flags.ecn = 1;
	if ((tcp_flags & COMM_TCPFLAG_CWR) != 0)
		tzsp_packet->parsed_data.transport_layer.tcp.flags.cwr = 1;
	if ((tcp_flags & COMM_TCPFLAG_NS) != 0)
		tzsp_packet->parsed_data.transport_layer.tcp.flags.ns = 1;

	return 1;

	//	if ( ((tcp_flags & COMM_TCPFLAG_SYN) != 0) || (((tcp_flags & COMM_TCPFLAG_SYN) != 0) && ((tcp_flags & COMM_TCPFLAG_ACK) != 0)))
	//	{
	//		KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "-----\n");
	//		KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "TZSP PID [%u] [%s -> %s]- SEQ/ACK [%X / %X]\n",
	//				tzsp_packet->packet_id, tzsp_packet->parsed_data.link_layer.ethernet.mac_src_str, tzsp_packet->parsed_data.link_layer.ethernet.mac_dst_str, ntohl(tcp_hdr->seq), ntohl(tcp_hdr->ack));
	//		if (!strncmp(tzsp_packet->parsed_data.network_layer.ipv4.src_str, "192", 3))
	//			logcolor = LOGCOLOR_PURPLE;
	//		if (!strncmp(tzsp_packet->parsed_data.network_layer.ipv4.dst_str, "192", 3))
	//			logcolor = LOGCOLOR_PURPLE;
	//		KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, logcolor, "IP [%s:%u -> %s:%u] - F [%u %s %s %s %s %s %s]\n",
	//				tzsp_packet->parsed_data.network_layer.ipv4.src_str, ntohs(tcp_hdr->src_port), tzsp_packet->parsed_data.network_layer.ipv4.dst_str, ntohs(tcp_hdr->dst_port), tcp_flags,
	//				(((tcp_flags & COMM_TCPFLAG_FIN) != 0) ? "FIN" : ""), (((tcp_flags & COMM_TCPFLAG_SYN) != 0) ? "SYN" : ""),
	//				(((tcp_flags & COMM_TCPFLAG_RST) != 0) ? "RST" : ""), (((tcp_flags & COMM_TCPFLAG_PSH) != 0) ? "PSH" : ""),
	//				(((tcp_flags & COMM_TCPFLAG_ACK) != 0) ? "ACK" : ""), (((tcp_flags & COMM_TCPFLAG_URG) != 0) ? "URG" : ""),
	//				(((tcp_flags & COMM_TCPFLAG_ECN) != 0) ? "ECN" : ""), (((tcp_flags & COMM_TCPFLAG_CWR) != 0) ? "CWR" : ""),
	//				(((tcp_flags & COMM_TCPFLAG_NS) != 0) ? "NS" : ""));
	//	}
}
/**************************************************************************************************************************/
static int CommEvTZSPServer_TransportLayerDissector_UDP(void *ev_tzsp_server_ptr, void *tzsp_packet_ptr, void *tzsp_tl_dissector_ptr, int proto_code)
{
	CommEvTZSPServer *ev_tzsp_server				= ev_tzsp_server_ptr;
	CommEvTZSPPacketParsed *tzsp_packet				= tzsp_packet_ptr;
	CommEvTZSPTransportLayerDissector *tl_dissector	= tzsp_tl_dissector_ptr;
	CommEvIPHeader *ip_hdr							= (CommEvIPHeader*)tzsp_packet->payload.network_layer.ptr;
	CommEvUDPHeader *udp_hdr						= (CommEvUDPHeader*)tzsp_packet->payload.transport_layer.ptr;

	KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "PACKET_ID [%d] - TL UDP [%u] - SRC/DST IP [%s]-[%s] - DATA_SZ [%u]\n",
			tzsp_packet->packet_id, ip_hdr->proto, tzsp_packet->parsed_data.network_layer.ipv4.src_str, tzsp_packet->parsed_data.network_layer.ipv4.dst_str,
			ntohs(ip_hdr->total_len));

	/* Set parsed packet flags as UDP */
	tzsp_packet->parsed_data.transport_layer.flags.udp = 1;
	return 1;
}
/**************************************************************************************************************************/
static int CommEvTZSPServer_TransportLayerDissector_ICMP(void *ev_tzsp_server_ptr, void *tzsp_packet_ptr, void *tzsp_tl_dissector_ptr, int proto_code)
{
	CommEvTZSPServer *ev_tzsp_server				= ev_tzsp_server_ptr;
	CommEvTZSPPacketParsed *tzsp_packet				= tzsp_packet_ptr;
	CommEvTZSPTransportLayerDissector *tl_dissector	= tzsp_tl_dissector_ptr;
	CommEvIPHeader *ip_hdr							= (CommEvIPHeader*)tzsp_packet->payload.network_layer.ptr;
	CommEvICMPHeader *icmp_hdr						= (CommEvICMPHeader*)tzsp_packet->payload.transport_layer.ptr;

	KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "PACKET_ID [%d] - TL ICMP [%u] - [%u / %u / %u / %u] - SRC/DST IP [%s]-[%s] - DATA_SZ [%u]\n",
			tzsp_packet->packet_id, ip_hdr->proto, icmp_hdr->icmp_type, icmp_hdr->icmp_code, icmp_hdr->icmp_id, icmp_hdr->icmp_seq, tzsp_packet->parsed_data.network_layer.ipv4.src_str, tzsp_packet->parsed_data.network_layer.ipv4.dst_str,
			ntohs(ip_hdr->total_len));

	/* Set parsed packet flags as ICMP */
	tzsp_packet->parsed_data.transport_layer.flags.icmp = 1;

	return 1;
}
/**************************************************************************************************************************/
static int CommEvTZSPServer_TransportLayerDissector_Unknown(void *ev_tzsp_server_ptr, void *tzsp_packet_ptr, void *tzsp_tl_dissector_ptr, int proto_code)
{
	CommEvTZSPServer *ev_tzsp_server				= ev_tzsp_server_ptr;
	CommEvTZSPPacketParsed *tzsp_packet				= tzsp_packet_ptr;
	CommEvTZSPTransportLayerDissector *tl_dissector	= tzsp_tl_dissector_ptr;

	KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "PACKET_ID [%d] - Unknown Transport Layer [%04X]\n", tzsp_packet->packet_id, proto_code);

	/* Set parsed packet flags as UNKNOWN */
	tzsp_packet->parsed_data.transport_layer.flags.unknown = 1;

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvTZSPServerConnStatsCalculate(CommEvTZSPServer *ev_tzsp_server)
{
	DLinkedListNode *node;
	struct timeval *current_tv;
	struct timeval *previous_tv;
	long delta_bytes;
	long delta_packets;
	int delta_time_ms;

	CommEvTZSPServerConn *conn	= NULL;
	EvKQBase *ev_base			= ev_tzsp_server->kq_base;

	/* Walk the list searching for node */
	for (node = ev_tzsp_server->conn.list.head; node; node = node->next)
	{
		conn = node->data;

		previous_tv		= &conn->time.last_stat_tv;
		current_tv		= &ev_base->stats.cur_invoke_tv;

		/* First iteration, update previous and leave */
		if (0 == previous_tv->tv_sec)
		{
			memcpy(previous_tv, current_tv, sizeof(struct timeval));
			continue;
		}

		/* Calculate delta */
		delta_time_ms	= EvKQBaseTimeValSubMsec(previous_tv, current_tv);

		if (delta_time_ms <= 0)
		{
			KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "CONN [%p] - Negative delta [%d] on stats calculation\n",
					conn, delta_time_ms);
			continue;
		}

		/* Calculate delta */
		delta_bytes		= conn->stats.total[COMM_CURRENT].bytes_rx - conn->stats.total[COMM_PREVIOUS].bytes_rx;
		delta_packets	= conn->stats.total[COMM_CURRENT].packet_rx - conn->stats.total[COMM_PREVIOUS].packet_rx;

		/* Calculate rate */
		conn->stats.rate.bytes_rx	= (float)(delta_bytes * 8000.00 / delta_time_ms);
		conn->stats.rate.packet_rx	= (float)(delta_packets * 1000.00 / delta_time_ms);

		KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "Calculated cur [%lu / %lu] delta [%ld / %ld] rate [%.2f %.2f]\n",
				conn->stats.total[COMM_CURRENT].bytes_rx, conn->stats.total[COMM_CURRENT].packet_rx, delta_bytes, delta_packets, conn->stats.rate.bytes_rx,
				conn->stats.rate.packet_rx);

		/* Update previous from current */
		conn->stats.total[COMM_PREVIOUS].bytes_rx		= conn->stats.total[COMM_CURRENT].bytes_rx;
		conn->stats.total[COMM_PREVIOUS].packet_rx		= conn->stats.total[COMM_CURRENT].packet_rx;

		/* Update time stamp */
		memcpy(previous_tv, current_tv, sizeof(struct timeval));
		continue;
	}

	return 0;
}
/**************************************************************************************************************************/
static int CommEvTZSPServerConnStatsUpdate(CommEvTZSPServerConn *conn, int data_sz)
{
	CommEvTZSPServer *ev_tzsp_server	= conn->parent;
	EvKQBase *kq_base					= ev_tzsp_server->kq_base;

	/* Update CONN data */
	memcpy(&conn->time.last_seen_tv, &kq_base->stats.cur_invoke_tv, sizeof(struct timeval));
	conn->time.last_seen_ts			= kq_base->stats.cur_invoke_ts_sec;
	conn->stats.total[COMM_CURRENT].bytes_rx	+= data_sz;
	conn->stats.total[COMM_CURRENT].packet_rx++;

	return 1;
}
/**************************************************************************************************************************/
static int CommEvTZSPServerConnTimer(int timer_id, int unused, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTZSPServer *ev_tzsp_server	= cb_data;
	EvKQBase *kq_base					= base_ptr;

	/* Invoke statistics calculation for origin streaming servers */
	CommEvTZSPServerConnStatsCalculate(ev_tzsp_server);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvTZSPServerEventRead(int fd, int can_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	struct sockaddr_in sender_addr;
	char read_buf[can_read_sz + 16];
	int read_sz;

	CommEvTZSPServer *ev_tzsp_server	= cb_data;
	CommEvTZSPServerConn *conn			= NULL;
	CommEvTZSPServerACL *acl			= NULL;
	EvKQBase *kq_base					= base_ptr;
	socklen_t addrlen					= sizeof(struct sockaddr_in);
	int remaining_read_sz				= can_read_sz;
	int total_read_sz					= 0;
	int packet_count					= 0;

	KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Receive TZSP data with [%d] bytes\n", fd, can_read_sz);

	while (remaining_read_sz > 0)
	{
		/* Receive TZSP packet data */
		memset(&read_buf, 0, sizeof(read_buf));
		read_sz	= recvfrom(fd, &read_buf, can_read_sz, 0, (struct sockaddr *) &sender_addr, &addrlen);

		/* Check ACL */
		//acl = CommEvTZSPServerACLAddrFind(ev_tzsp_server, &sender_addr);

		/* Failed reading, stop */
		if (read_sz <= 0)
		{
			KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_DEBUG, LOGCOLOR_RED, "FD [%d] - PACKET [%d] - Failed reading\n", fd, packet_count);
			break;
		}

		/* Lookup or create a new connection for this SENDER ADDR */
		conn = CommEvTZSPServerConnAddrLookupOrNew(ev_tzsp_server, &sender_addr);

		KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d / %p] - IP [%s] - PACKET [%d] - Processing [%d] bytes\n",
				fd, conn, conn->addr_str, packet_count, read_sz);

		/* Process TZSP packet */
		CommEvTZSPServerTZSP_PacketProcess(ev_tzsp_server, packet_count, (char*)&read_buf, read_sz);

		/* Update statistics */
		CommEvTZSPServerConnStatsUpdate(conn, read_sz);

		/* Increment total read, decrement remaining read and touch packet count */
		remaining_read_sz	-= read_sz;
		total_read_sz		+= read_sz;
		packet_count++;
		continue;
	}

	/* Update statistics */
	ev_tzsp_server->stats.recv_bytes += total_read_sz;
	ev_tzsp_server->stats.pkt_count	 += packet_count;

	/* Reschedule READ_EV */
	EvKQBaseSetEvent(ev_tzsp_server->kq_base, ev_tzsp_server->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTZSPServerEventRead, ev_tzsp_server);
	return 1;
}
/**************************************************************************************************************************/
