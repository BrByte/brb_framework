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

#include "../include/libbrb_ev_kq.h"

/* TZSP layer processing PROTOTYPES */
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
static CommEvTZSPNetworkLayerDissectorFunc CommEvTZSPServer_NetworkLayerDissector_Unknown;

/* Internal TRANSPORT LAYER DISSECTORS */
static CommEvTZSPTransportLayerDissectorFunc CommEvTZSPServer_TransportLayerDissector_TCP;
static CommEvTZSPTransportLayerDissectorFunc CommEvTZSPServer_TransportLayerDissector_UDP;
static CommEvTZSPTransportLayerDissectorFunc CommEvTZSPServer_TransportLayerDissector_ICMP;
static CommEvTZSPTransportLayerDissectorFunc CommEvTZSPServer_TransportLayerDissector_Unknown;

/* Internal IO events */
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

	KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "New TZSP_SERVER listening on UDP_PORT [%d]\n", ev_tzsp_server->port);

	/* Register all protocol DISSECTORs */
	CommEvTZSPServerDissectorRegisterAll(ev_tzsp_server);

	/* Set read internal events for UDP server socket and FD_DESCRIPTION */
	EvKQBaseSetEvent(ev_tzsp_server->kq_base, ev_tzsp_server->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTZSPServerEventRead, ev_tzsp_server);
	EvKQBaseFDDescriptionSetByFD(ev_tzsp_server->kq_base, ev_tzsp_server->socket_fd, "BRB_EV_COMM - TZSP SERVER on PORT [%u]", htons(ev_tzsp_server->port));

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
	/* Sanity check */
	if (!ev_tzsp_server)
		return;

	/* Shutdown and free */
	CommEvTZSPServerShutdown(ev_tzsp_server);
	free(ev_tzsp_server);
	return;
}
/**************************************************************************************************************************/
void CommEvTZSPServerDissectorRegisterAll(CommEvTZSPServer *ev_tzsp_server)
{
	CommEvTZSPLinkLayerDissector *lld_ethernet;
	CommEvTZSPLinkLayerDissector *lld_unknown;
	CommEvTZSPNetworkLayerDissector *nld_ipv4;
	CommEvTZSPNetworkLayerDissector *nld_unknown;
	CommEvTZSPTransportLayerDissector *tld_tcp;
	CommEvTZSPTransportLayerDissector *tld_udp;
	CommEvTZSPTransportLayerDissector *tld_icmp;
	CommEvTZSPTransportLayerDissector *tld_unknown;

	/* Register LINK_LAYER DISSECTORs */
	lld_ethernet	= CommEvTZSPServerLinkLayerDissectorRegister(ev_tzsp_server, CommEvTZSPServer_LinkLayerDissector_Ethernet, COMM_TZSP_ENCAP_ETHERNET, COMM_ETHERNET_FRAME_SIZE);
	lld_unknown		= CommEvTZSPServerLinkLayerDissectorRegister(ev_tzsp_server, CommEvTZSPServer_LinkLayerDissector_Unknown, COMM_TZSP_ENCAP_UNKNOWN, 0);

	/* Register NETWORK LAYER DISSECTORs */
	nld_ipv4	= CommEvTZSPServerNetworkLayerDissectorRegister(ev_tzsp_server, lld_ethernet, CommEvTZSPServer_NetworkLayerDissector_IPv4, COMM_ETHERNET_ENCAP_IPV4, COMM_IPV4_HEADER_SIZE);
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
		return 0;

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
			//tag_cur_uc_value = tzsp_tag_arr_ptr[tag_cur_offset + 2];
			//KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "PACKET_ID [%d] - RAW RSSI [%u]\n", packet_id, ntohs(tag_cur_uc_value));
			break;
		}

		case COMM_TZSP_TAG_SNR:
		{
			break;
		}

		case COMM_TZSP_TAG_DATA_RATE:
		{
			break;
		}

		case COMM_TZSP_TAG_TIMESTAMP:
		{
			break;
		}

		case COMM_TZSP_TAG_CONTENTION_FREE:
		{
			break;
		}

		case COMM_TZSP_TAG_DECRYPT_FAIL:
		{
			break;
		}

		case COMM_TZSP_TAG_FCS_ERROR:
		{
			break;
		}

		case COMM_TZSP_TAG_RX_CHANNEL:
		{
			break;
		}

		case COMM_TZSP_TAG_PACKET_COUNT:
		{
			break;
		}

		case COMM_TZSP_TAG_FRAME_LENGTH:
		{
			break;
		}

		case COMM_TZSP_TAG_RADIO_HDR_SERIAL:
		{
			break;
		}

		default:
		{
			break;
		}
		}

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
		if (tzsp_packet->tzsp.proto.id == ll_dissector->encap_code)
		{
			KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "PACKET_ID [%d] - MATCH_COUNT [%d] - LinkLayer detected [%d]-[%s]\n",
					tzsp_packet->packet_id, match_count, ll_dissector->encap_code, ll_dissector->encap_desc);

			/* Adjust TZSP packet for this LINKLAYER and touch MATCH_COUNT */
			tzsp_packet->payload.link_layer.size	= ll_dissector->header_size;
			tzsp_packet->payload.network_layer.ptr	= (tzsp_packet->payload.link_layer.ptr + tzsp_packet->payload.link_layer.size);
			match_count++;

			/* Invoke LINKLAYER DISSECTOR function - It will return NETWORK LAYER protocol code for further examination */
			nl_proto	= ll_dissector->cb_func(ev_tzsp_server, tzsp_packet, ll_dissector);

			/* Dispatch to NETWORK LAYER */
			CommEvTZSPServer_NetworkLayerDissector_Dispatch(ev_tzsp_server, tzsp_packet, ll_dissector, nl_proto);
			continue;
		}

		continue;
	}

	/* None found, invoke UNKNOWN PROTO DISSECTOR */
	if ((ll_dissector_unknown) && (0 == match_count))
	{
		KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "PACKET_ID [%d] - LinkLayer unknown [%d] - Will invoke UNKNOWN DISSECTOR\n",
				tzsp_packet->packet_id, tzsp_packet->tzsp.proto.id);

		/* Adjust TZSP packet for this LINKLAYER */
		tzsp_packet->payload.link_layer.size	= ll_dissector_unknown->header_size;
		tzsp_packet->payload.network_layer.ptr	= (tzsp_packet->payload.link_layer.ptr + tzsp_packet->payload.link_layer.size);

		/* Invoke UNKNOWN LINKLAYER DISSECTOR function */
		nl_proto = ll_dissector_unknown->cb_func(ev_tzsp_server, tzsp_packet, ll_dissector_unknown);

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

	//KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "PACKET_ID [%d] - NET_PROTO [0x%04X]\n", tzsp_packet->packet_id, proto_code);

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
		if (proto_code == nl_dissector->proto_code)
		{
			KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "PACKET_ID [%d] - MATCH_COUNT [%d] - NetworkLayer detected [%d]-[%s]\n",
					tzsp_packet->packet_id, match_count, nl_dissector->proto_code, nl_dissector->proto_desc);

			/* Adjust TZSP packet for this NETWORK LAYER and touch MATCH_COUNT */
			tzsp_packet->payload.network_layer.size		= nl_dissector->header_size;
			tzsp_packet->payload.transport_layer.ptr	= (tzsp_packet->payload.network_layer.ptr + tzsp_packet->payload.network_layer.size);
			match_count++;

			/* Invoke NETWORK LAYER DISSECTOR function - It will return TRANSPORT LAYER protocol code for further examination */
			tl_proto	= nl_dissector->cb_func(ev_tzsp_server, tzsp_packet, nl_dissector);

			/* Dispatch to TRANSPORT LAYER */
			CommEvTZSPServer_TransportLayerDissector_Dispatch(ev_tzsp_server, tzsp_packet, nl_dissector, tl_proto);
		}

		continue;
	}

	/* None found, invoke UNKNOWN PROTO DISSECTOR */
	if ((nl_dissector_unknown) && (0 == match_count))
	{
		KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "PACKET_ID [%d] - NetworkLayer unknown [%d] - Will invoke UNKNOWN DISSECTOR\n",
				tzsp_packet->packet_id, proto_code);

		/* Adjust TZSP packet for this LINKLAYER */
		tzsp_packet->payload.network_layer.size		= nl_dissector_unknown->header_size;
		tzsp_packet->payload.transport_layer.ptr	= (tzsp_packet->payload.network_layer.ptr + tzsp_packet->payload.network_layer.size);

		/* Invoke UNKNOWN LINKLAYER DISSECTOR function */
		tl_proto = nl_dissector_unknown->cb_func(ev_tzsp_server, tzsp_packet, nl_dissector_unknown);

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

		//assert(tl_dissector->header_size > 0);

		/* Found matching PROTO, dispatch to TRANSPORT_LAYER_DISSECTOR */
		if (proto_code == tl_dissector->proto_code)
		{
			KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "PACKET_ID [%d] - MATCH_COUNT [%d] - TransportLayer detected [%d]-[%s]\n",
					tzsp_packet->packet_id, match_count, tl_dissector->proto_code, tl_dissector->proto_desc);

			/* Adjust TZSP packet for this NETWORK LAYER and touch MATCH_COUNT */
			tzsp_packet->payload.transport_layer.size	= tl_dissector->header_size;
			match_count++;

			/* Invoke TRANSPORT LAYER DISSECTOR function  */
			tl_dissector->cb_func(ev_tzsp_server, tzsp_packet, nl_dissector);
		}

		continue;
	}

	/* None found, invoke UNKNOWN PROTO DISSECTOR */
	if ((tl_dissector_unknown) && (0 == match_count))
	{
		KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "PACKET_ID [%d] - NetworkLayer unknown [%d] - Will invoke UNKNOWN DISSECTOR\n",
				tzsp_packet->packet_id, proto_code);

		/* Adjust TZSP packet for this LINKLAYER */
		tzsp_packet->payload.transport_layer.size		= tl_dissector_unknown->header_size;

		/* Invoke UNKNOWN TRANSPORT LAYER DISSECTOR function */
		tl_dissector_unknown->cb_func(ev_tzsp_server, tzsp_packet, tl_dissector_unknown);
		return 0;
	}

	return match_count;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvTZSPServer_LinkLayerDissector_Ethernet(void *ev_tzsp_server_ptr, void *tzsp_packet_ptr, void *tzsp_ll_dissector_ptr)
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

	KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "PACKET_ID [%d] - MAC SRC/DST [%s]-[%s] - NL_PROTO [0x%04X]\n",
			tzsp_packet->packet_id, tzsp_packet->parsed_data.link_layer.ethernet.mac_src_str, tzsp_packet->parsed_data.link_layer.ethernet.mac_dst_str, nl_proto_code);

	//EvKQBaseLoggerHexDump(ev_tzsp_server->log_base, LOGTYPE_CRITICAL, tzsp_packet->payload.proto_ptr, 32, 8, 4);
	return nl_proto_code;
}
/**************************************************************************************************************************/
static int CommEvTZSPServer_LinkLayerDissector_Unknown(void *ev_tzsp_server_ptr, void *tzsp_packet_ptr, void *tzsp_ll_dissector_ptr)
{
	CommEvTZSPServer *ev_tzsp_server			= ev_tzsp_server_ptr;
	CommEvTZSPPacketParsed *tzsp_packet			= tzsp_packet_ptr;
	CommEvTZSPLinkLayerDissector *ll_dissector	= tzsp_ll_dissector_ptr;

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvTZSPServer_NetworkLayerDissector_IPv4(void *ev_tzsp_server_ptr, void *tzsp_packet_ptr, void *tzsp_nl_dissector_ptr)
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

	KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "PACKET_ID [%d] - PROTO [%u] - SRC/DST IP [%s]-[%s] - DATA_SZ [%u]\n",
			tzsp_packet->packet_id, ip_hdr->proto, tzsp_packet->parsed_data.network_layer.ipv4.src_str, tzsp_packet->parsed_data.network_layer.ipv4.dst_str,
			ntohs(ip_hdr->total_len));

	return ip_hdr->proto;
}
/**************************************************************************************************************************/
static int CommEvTZSPServer_NetworkLayerDissector_Unknown(void *ev_tzsp_server_ptr, void *tzsp_packet_ptr, void *tzsp_nl_dissector_ptr)
{
	CommEvTZSPServer *ev_tzsp_server				= ev_tzsp_server_ptr;
	CommEvTZSPPacketParsed *tzsp_packet				= tzsp_packet_ptr;
	CommEvTZSPNetworkLayerDissector *nl_dissector	= tzsp_nl_dissector_ptr;

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvTZSPServer_TransportLayerDissector_TCP(void *ev_tzsp_server_ptr, void *tzsp_packet_ptr, void *tzsp_tl_dissector_ptr)
{
	CommEvTZSPServer *ev_tzsp_server				= ev_tzsp_server_ptr;
	CommEvTZSPPacketParsed *tzsp_packet				= tzsp_packet_ptr;
	CommEvTZSPTransportLayerDissector *tl_dissector	= tzsp_tl_dissector_ptr;
	CommEvTCPHeader *tcp_hdr						= (CommEvTCPHeader*)tzsp_packet->payload.transport_layer.ptr;

	KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "PACKET_ID [%d] - TCP_SEQ [%u] - SRC/DST PORT [%u / %u]\n",
			tzsp_packet->packet_id, ntohs(tcp_hdr->seq), ntohs(tcp_hdr->src_port), ntohs(tcp_hdr->dst_port));

	return 1;
}
/**************************************************************************************************************************/
static int CommEvTZSPServer_TransportLayerDissector_UDP(void *ev_tzsp_server_ptr, void *tzsp_packet_ptr, void *tzsp_tl_dissector_ptr)
{
	CommEvTZSPServer *ev_tzsp_server				= ev_tzsp_server_ptr;
	CommEvTZSPPacketParsed *tzsp_packet				= tzsp_packet_ptr;
	CommEvTZSPTransportLayerDissector *tl_dissector	= tzsp_tl_dissector_ptr;

	return 1;
}
/**************************************************************************************************************************/
static int CommEvTZSPServer_TransportLayerDissector_ICMP(void *ev_tzsp_server_ptr, void *tzsp_packet_ptr, void *tzsp_tl_dissector_ptr)
{
	CommEvTZSPServer *ev_tzsp_server				= ev_tzsp_server_ptr;
	CommEvTZSPPacketParsed *tzsp_packet				= tzsp_packet_ptr;
	CommEvTZSPTransportLayerDissector *tl_dissector	= tzsp_tl_dissector_ptr;

	return 1;
}
/**************************************************************************************************************************/
static int CommEvTZSPServer_TransportLayerDissector_Unknown(void *ev_tzsp_server_ptr, void *tzsp_packet_ptr, void *tzsp_tl_dissector_ptr)
{
	CommEvTZSPServer *ev_tzsp_server				= ev_tzsp_server_ptr;
	CommEvTZSPPacketParsed *tzsp_packet				= tzsp_packet_ptr;
	CommEvTZSPTransportLayerDissector *tl_dissector	= tzsp_tl_dissector_ptr;

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvTZSPServerEventRead(int fd, int can_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	struct sockaddr_in sender_addr;
	char read_buf[can_read_sz + 16];
	int read_sz;

	CommEvTZSPServer *ev_tzsp_server	= cb_data;
	EvKQBase *kq_base					= base_ptr;
	socklen_t addrlen					= sizeof(struct sockaddr_in);
	int remaining_read_sz				= can_read_sz;
	int total_read_sz					= 0;
	int packet_count					= 0;

	KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Receive TZSP data with [%d] bytes\n", fd, can_read_sz);

	while (remaining_read_sz > 0)
	{
		/* Receive TZSP packet data */
		memset(&read_buf, 0, sizeof(read_buf));
		read_sz	= recvfrom(fd, &read_buf, can_read_sz, 0, (struct sockaddr *) &sender_addr, &addrlen);

		/* Failed reading, stop */
		if (read_sz <= 0)
		{
			KQBASE_LOG_PRINTF(ev_tzsp_server->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - PACKET [%d] - Failed reading\n", fd, packet_count);
			break;
		}

		/* Process TZSP packet */
		CommEvTZSPServerTZSP_PacketProcess(ev_tzsp_server, packet_count, (char*)&read_buf, read_sz);

		/* Increment total read, decrement remaining read and touch packet count */
		remaining_read_sz	-= read_sz;
		total_read_sz		+= read_sz;
		packet_count++;
		continue;
	}

	/* Reschedule READ_EV */
	EvKQBaseSetEvent(ev_tzsp_server->kq_base, ev_tzsp_server->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTZSPServerEventRead, ev_tzsp_server);
	return 1;
}
/**************************************************************************************************************************/
