/*
 * libbrb_comm_utils.h
 *
 *  Created on: 2016-10-05
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2016 BrByte Software (Oliveira Alves & Amorim LTDA)
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

#ifndef LIBBRB_COMM_UTILS_H_
#define LIBBRB_COMM_UTILS_H_

/******************************************************************************************************/
/**/
/**/
/******************************************************************************************************/
#define TFTP_ACK				1
#define TFTP_FIN				2
#define TFTP_DEFAULT_PORT		69
#define TFTP_HEADER_SIZE		4
#define TFTP_PAYLOAD_SIZE		512
#define TFTP_TOT_PACKET_SIZE	(TFTP_HEADER_SIZE + TFTP_PAYLOAD_SIZE)
/*******************************************************/
typedef void CommEvTFTPTServerCBH(void*, void*, int, int, char *, struct sockaddr_in *); /* BASE, CBDATA, OPERATION, TRANSFER_MODE, FILENAME, CLI_ADDR */
/*******************************************************/
typedef enum
{
	COMM_TFTP_SERVER_FAILURE_SOCKET,
	COMM_TFTP_SERVER_FAILURE_REUSEADDR,
	COMM_TFTP_SERVER_FAILURE_REUSEPORT,
	COMM_TFTP_SERVER_FAILURE_BIND,
	COMM_TFTP_SERVER_FAILURE_LISTEN,
	COMM_TFTP_SERVER_FAILURE_SETNONBLOCKING,
	COMM_TFTP_SERVER_FAILURE_UNKNOWN,
	COMM_TFTP_SERVER_INIT_OK
} CommEvTFTPServerStartCodes;

typedef enum
{
	COMM_TFTP_EVENT_FILE_READ,
	COMM_TFTP_EVENT_FILE_WRITE,
	COMM_TFTP_EVENT_FILE_FINISH,
	COMM_TFTP_EVENT_LASTITEM,
} CommEvTFTPEventCodes;

typedef enum
{
	COMM_TFTP_TRANSFER_NETASCII,
	COMM_TFTP_TRANSFER_OCTET,
	COMM_TFTP_TRANSFER_LASTITEM
} CommEvTFTPTransferCodes;

typedef enum
{
	COMM_TFTP_ERROR_UNDEFINED,
	COMM_TFTP_ERROR_FILE_NOTFOUND,
	COMM_TFTP_ERROR_ACCCESS_VIOLATION,
	COMM_TFTP_ERROR_DISK_FULL,
	COMM_TFTP_ERROR_ILLEGAL_OPERATION,
	COMM_TFTP_ERROR_UNKNOWN_TRANSFER_ID,
	COMM_TFTP_ERROR_FILE_EXISTS,
	COMM_TFTP_ERROR_NO_SUCH_USER,
	COMM_TFTP_ERROR_UNACCEPTABLE_OPTION,
	OMM_TFTP_ERROR_LASTITEM,
} CommEvTFTPErrorCodes;

typedef enum
{
	COMM_TFTP_OPCODE_NONE,	/* 0 */
	COMM_TFTP_OPCODE_RRQ,	/* 1 */
	COMM_TFTP_OPCODE_WRQ,	/* 2 */
	COMM_TFTP_OPCODE_DATA,	/* 3 */
	COMM_TFTP_OPCODE_ACK,	/* 4 */
	COMM_TFTP_OPCODE_ERROR,	/* 5 */
	COMM_TFTP_OPCODE_OACK	/* 6 */
} CommEvTFTPOpcodes;
/*******************************************************/
typedef struct _CommEvTFTPTransfer
{
	DLinkedListNode node;
	MemBuffer *data_mb;
	struct _CommEvTFTPServer *ev_tftp_server;
	struct sockaddr_in cli_addr;
	long cur_offset;
	long cur_block;
	int operation;

	struct
	{
		unsigned int pending_io:1;
		unsigned int pending_ack:1;
		unsigned int finished:1;
	} flags;
} CommEvTFTPTransfer;

typedef struct _CommEvTFTPPacket
{
	unsigned int opcode:16;
	unsigned int seq_id:16;
	unsigned char data[TFTP_PAYLOAD_SIZE];
} CommEvTFTPPacket;

typedef struct _CommEvTFTPControlPacket
{
	unsigned int opcode:16;
	unsigned char data[TFTP_PAYLOAD_SIZE];
} CommEvTFTPControlPacket;

typedef struct _CommEvTFTPServerConf
{
	int port;
} CommEvTFTPServerConf;

typedef struct _CommEvTFTPServer
{
	struct _EvKQBase *kq_base;
	struct _EvKQBaseLogBase *log_base;

	struct sockaddr_in addr;
	int socket_fd;
	int port;

	struct
	{
		CommEvTFTPTServerCBH *cb_handler_ptr;
		void *cb_data_ptr;
		struct
		{
			unsigned int enabled :1;
		} flags;
	} events[COMM_TFTP_EVENT_LASTITEM];

	struct
	{
		DLinkedList read_list;
		DLinkedList write_list;
	} pending;

	struct
	{
		long sent_bytes;
		long recv_bytes;
	} stats;

} CommEvTFTPServer;
/******************************************************************************************************/
/**/
/**/
/******************************************************************************************************/
#define TZSP_DEFAULT_PORT				37008
#define TZSP_LINKLAYER_DISSECTOR_MAX	32
#define TZSP_NETWORK_DISSECTOR_MAX		32
#define TZSP_TRANSPORT_DISSECTOR_MAX	32

/*******************************************************/
typedef int CommEvTZSPLinkLayerDissectorFunc(void *, void *, void *);		/* TZSP_SERVER, TZSP_PACKET, TZSP_MEDIA_DISSECTOR */
typedef int CommEvTZSPNetworkLayerDissectorFunc(void *, void *, void *);	/* TZSP_SERVER, TZSP_PACKET, TZSP_MEDIA_DISSECTOR */
typedef int CommEvTZSPTransportLayerDissectorFunc(void *, void *, void *);	/* TZSP_SERVER, TZSP_PACKET, TZSP_MEDIA_DISSECTOR */
typedef void CommEvTZSPServerCBH(void*, void*, int);						/* BASE, CBDATA, OPERATION, TRANSFER_MODE, FILENAME, CLI_ADDR */
/*******************************************************/
typedef enum
{
	COMM_TZSP_SERVER_FAILURE_SOCKET,
	COMM_TZSP_SERVER_FAILURE_REUSEADDR,
	COMM_TZSP_SERVER_FAILURE_REUSEPORT,
	COMM_TZSP_SERVER_FAILURE_BIND,
	COMM_TZSP_SERVER_FAILURE_LISTEN,
	COMM_TZSP_SERVER_FAILURE_SETNONBLOCKING,
	COMM_TZSP_SERVER_FAILURE_UNKNOWN,
	COMM_TZSP_SERVER_INIT_OK
} CommEvTZSPServerStartCodes;

typedef enum
{
	COMM_TZSP_EVENT_PACKET,
	COMM_TZSP_EVENT_LASTITEM,
} CommEvTZSPEventCodes;

typedef enum
{
	COMM_TZSP_PACKET_TYPE_RX,
	COMM_TZSP_PACKET_TYPE_TX,
	COMM_TZSP_PACKET_TYPE_RESERVED,
	COMM_TZSP_PACKET_TYPE_CONFIG,
	COMM_TZSP_PACKET_TYPE_KEEP_ALIVE,
	COMM_TZSP_PACKET_TYPE_PORT_OPENER,
	COMM_TZSP_PACKET_TYPE_LASTITEM
} CommEvTZSPPacketTypeCodes;

typedef enum
{
	/* Padding and END mark */
	COMM_TZSP_TAG_PADDING			= 0x00,
	COMM_TZSP_TAG_END				= 0x01,
	/* 802.11 related TAGs */
	COMM_TZSP_TAG_RAW_RSSI			= 0x0A,		/* DEC 10 - Signal strength in dBm, signed byte. */
	COMM_TZSP_TAG_SNR				= 0x0B,		/* DEC 11 - Noise level in dBm, signed byte. */
	COMM_TZSP_TAG_DATA_RATE			= 0x0C,		/* DEC 12 - Data rate, unsigned byte. */
	COMM_TZSP_TAG_TIMESTAMP			= 0x0D,		/* DEC 13 - TIMESTAMP in uS, unsigned 32-bits network byte order. */
	COMM_TZSP_TAG_PACKET_TYPE		= 0x0E,		/* DEC 14 - Packet type, unsigned byte. */
	COMM_TZSP_TAG_CONTENTION_FREE	= 0x0F,		/* DEC 15 - Whether packet arrived during contention-free period, unsigned byte. */
	COMM_TZSP_TAG_DECRYPT_FAIL		= 0x10,		/* DEC 16 - Whether packet could not be DECRYPTED by MAC, unsigned byte. */
	COMM_TZSP_TAG_FCS_ERROR			= 0x11,		/* DEC 17 - Whether packet contains an FCS error, unsigned byte. */
	COMM_TZSP_TAG_RX_CHANNEL		= 0x12,		/* DEC 18 - Channel number packet was received on, unsigned byte.*/
	/* Generic HEADER options */
	COMM_TZSP_TAG_WLAN_STA			= 0x1E,		/* DEC 30 - Station statistics */
	COMM_TZSP_TAG_WLAN_PKT			= 0x1F,		/* DEC 31 - Packet statistics */
	COMM_TZSP_TAG_PACKET_COUNT		= 0x28,		/* DEC 40 - Unique ID of the packet */
	COMM_TZSP_TAG_FRAME_LENGTH		= 0x29,		/* DEC 41 - Length of the packet before slicing. 2 bytes. */
	COMM_TZSP_TAG_RADIO_HDR_SERIAL	= 0x3C		/* DEC 60 - Sensor MAC address packet was received on, 6 byte ETHERNET address.*/
} CommEvTZSPTagCodes;

typedef enum
{
	COMM_TZSP_ENCAP_ETHERNET			= 1,
	COMM_TZSP_ENCAP_TOKEN_RING			= 2,
	COMM_TZSP_ENCAP_SLIP				= 3,
	COMM_TZSP_ENCAP_PPP					= 4,
	COMM_TZSP_ENCAP_FDDI				= 5,
	COMM_TZSP_ENCAP_RAW					= 7,
	COMM_TZSP_ENCAP_IEEE_802_11			= 18,
	COMM_TZSP_ENCAP_IEEE_802_11_PRISM	= 119,
	COMM_TZSP_ENCAP_IEEE_802_11_AVS		= 127,
	COMM_TZSP_ENCAP_UNKNOWN				= 254,
} CommEvTZSPEncapCodes;
/*******************************************************/
typedef struct _CommEvTZSPHeader
{
	unsigned char version;
	unsigned char type;
	unsigned short protocol;
} CommEvTZSPHeader;

typedef struct _CommEvTZSPTransportLayerDissector
{
	CommEvTZSPTransportLayerDissectorFunc *cb_func;
	char *proto_desc;
	int nl_dissector_id;
	int proto_code;
	int header_size;
	int id;
} CommEvTZSPTransportLayerDissector;

typedef struct _CommEvTZSPNetworkLayerDissector
{
	CommEvTZSPNetworkLayerDissectorFunc *cb_func;
	char *proto_desc;
	int ll_dissector_id;
	int proto_code;
	int header_size;
	int id;

	struct
	{
		CommEvTZSPTransportLayerDissector arr[TZSP_TRANSPORT_DISSECTOR_MAX];
		int count;
	} dissector;

} CommEvTZSPNetworkLayerDissector;

typedef struct _CommEvTZSPLinkLayerDissector
{
	CommEvTZSPLinkLayerDissectorFunc *cb_func;
	char *encap_desc;
	int encap_code;
	int header_size;
	int id;

	struct
	{
		CommEvTZSPNetworkLayerDissector arr[TZSP_NETWORK_DISSECTOR_MAX];
		int count;
	} dissector;

} CommEvTZSPLinkLayerDissector;

typedef struct _CommEvTZSPPacketParsed
{
	int packet_id;

	struct
	{
		struct
		{
			char *desc_str;
			int id;
		} proto;

		struct
		{
			unsigned int timestamp;
			unsigned int packet_count;
			unsigned char rx_channel;
			unsigned char fcs_error;
			unsigned char decrypted;
			unsigned char contention_free;
			unsigned short rx_frame_length;
			short raw_rssi;
			short snr;
			short data_rate;
		} tag;

	} tzsp;

	struct
	{
		int size_total;

		struct
		{
			char *ptr;
			int size;
		} link_layer;

		struct
		{
			char *ptr;
			int size;
		} network_layer;

		struct
		{
			char *ptr;
			int size;
		} transport_layer;

	} payload;

	struct
	{
		struct
		{
			struct
			{
				char mac_src_str[20];
				char mac_dst_str[20];
				int proto_id;
			} ethernet;
		} link_layer;

		struct
		{
			struct
			{
				char src_str[32];
				char dst_str[32];
				int proto_id;
				int payload_sz;
			} ipv4;
		} network_layer;

	} parsed_data;

} CommEvTZSPPacketParsed;

typedef struct _CommEvTZSPServerConf
{
	struct _EvKQBaseLogBase *log_base;
	int port;
} CommEvTZSPServerConf;

typedef struct _CommEvTZSPServer
{
	struct _EvKQBase *kq_base;
	struct _EvKQBaseLogBase *log_base;
	struct sockaddr_in addr;
	int socket_fd;
	int port;

	struct
	{
		CommEvTZSPServerCBH *cb_handler_ptr;
		void *cb_data_ptr;
		struct
		{
			unsigned int enabled :1;
		} flags;
	} events[COMM_TZSP_EVENT_LASTITEM];

	struct
	{
		CommEvTZSPLinkLayerDissector arr[TZSP_LINKLAYER_DISSECTOR_MAX];
		int count;
	} dissector;

	struct
	{
		long sent_bytes;
		long recv_bytes;
	} stats;

} CommEvTZSPServer;
/*******************************************************/
static const CommEvToken comm_tzsp_tag_desc_arr[] =
{
		{"PADDING",				COMM_TZSP_TAG_PADDING},
		{"END",					COMM_TZSP_TAG_END},
		{"RAW RSSI",			COMM_TZSP_TAG_RAW_RSSI},
		{"SNR",					COMM_TZSP_TAG_SNR},
		{"DATA RATE",			COMM_TZSP_TAG_DATA_RATE},
		{"TIMESTAMP",			COMM_TZSP_TAG_TIMESTAMP},
		{"CONTENTION FREE",		COMM_TZSP_TAG_CONTENTION_FREE},
		{"DECRYPTED",			COMM_TZSP_TAG_DECRYPT_FAIL},
		{"FCS ERROR",			COMM_TZSP_TAG_FCS_ERROR},
		{"RX CHANNEL",			COMM_TZSP_TAG_RX_CHANNEL},
		{"PACKET COUNT",		COMM_TZSP_TAG_PACKET_COUNT},
		{"FRAME LENGTH",		COMM_TZSP_TAG_FRAME_LENGTH},
		{"RADIO HDR SERIAL",	COMM_TZSP_TAG_RADIO_HDR_SERIAL},
		{NULL, 0}
};

static const CommEvToken comm_tzsp_encap_desc_arr[] =
{
		{"ETHERNET",			COMM_TZSP_ENCAP_ETHERNET},
		{"TOKEN_RING",			COMM_TZSP_ENCAP_TOKEN_RING},
		{"SLIP",				COMM_TZSP_ENCAP_SLIP},
		{"PPP",					COMM_TZSP_ENCAP_PPP},
		{"FDDI",				COMM_TZSP_ENCAP_FDDI},
		{"RAW",					COMM_TZSP_ENCAP_RAW},
		{"IEEE 802.11",			COMM_TZSP_ENCAP_IEEE_802_11},
		{"IEEE 802.11 PRISM",	COMM_TZSP_ENCAP_IEEE_802_11_PRISM},
		{"IEEE 802.11 AVS",		COMM_TZSP_ENCAP_IEEE_802_11_AVS},
		{NULL, 0}
};

static const CommEvToken comm_tzsp_ethertype_desc_arr[] =
{
		{"IPv4",							COMM_ETHERNET_ENCAP_IPV4},
		{"ARP",								COMM_ETHERNET_ENCAP_ARP},
		{"WakeUp on LAN",					COMM_ETHERNET_ENCAP_WOL},
		{"Audio Video Transport Protocol",	COMM_ETHERNET_ENCAP_AVTP},
		{"Reverse ARP",						COMM_ETHERNET_ENCAP_RARP},
		{"VLAN Tag",						COMM_ETHERNET_ENCAP_VLAN},
		{"IPX",								COMM_ETHERNET_ENCAP_IPX0},
		{"IPX",								COMM_ETHERNET_ENCAP_IPX1},
		{"IPv6",							COMM_ETHERNET_ENCAP_IPV6},
		{"Ethernet Flow Control",			COMM_ETHERNET_ENCAP_EFC},
		{"MPLS Unicast",					COMM_ETHERNET_ENCAP_MPLS_U},
		{"MPLS Multicast",					COMM_ETHERNET_ENCAP_MPLS_M},
		{"PPPoE Discovery",					COMM_ETHERNET_ENCAP_PPPOE_D},
		{"PPPoE Session",					COMM_ETHERNET_ENCAP_PPPOE_S},
		{"Jumbo Frame",						COMM_ETHERNET_ENCAP_JUMBO},
		{"ATA over Ethernet",				COMM_ETHERNET_ENCAP_ATAOE},
		{"Link Layer Discovery Protocol",	COMM_ETHERNET_ENCAP_LLDP},
		{"MACSec",							COMM_ETHERNET_ENCAP_MACSEC},
		{NULL, 0}
};

static const CommEvToken comm_tzsp_ipproto_desc_arr[] =
{
		{"IPv6 Hop-by-Hop Option",		COMM_IPPROTO_HOPOPT},
		{"ICMP",						COMM_IPPROTO_ICMP},
		{"IGMP",						COMM_IPPROTO_IGMP},
		{"Gateway-to-Gateway Protocol",	COMM_IPPROTO_GGP},
		{"IP-IP Encap",					COMM_IPPROTO_IP_IP},
		{"Internet Stream Protocol",	COMM_IPPROTO_ST},
		{"TCP",							COMM_IPPROTO_TCP},
		{"Core-based trees",			COMM_IPPROTO_CBT},
		{"EGP",							COMM_IPPROTO_EGP},
		{"IGP",							COMM_IPPROTO_IGP},
		{"BBN RCC Monitoring",			COMM_IPPROTO_BBN_RCC_MON},
		{"Network Voice Protocol",		COMM_IPPROTO_NVP_II},
		{"Xerox PUP",					COMM_IPPROTO_PUP},
		{"ARGUS",						COMM_IPPROTO_ARGUS},
		{"EMCON",						COMM_IPPROTO_EMCON},
		{"Cross Net Debugger",			COMM_IPPROTO_XNET},
		{"CHAOS",						COMM_IPPROTO_CHAOS},
		{"UDP",							COMM_IPPROTO_UDP},
		{"Multiplexing",				COMM_IPPROTO_MUX},
		{"DCN Measurement Subsystems",	COMM_IPPROTO_DCN_MEAS},
		{"Host Monitoring Protocol",	COMM_IPPROTO_HMP},
		{"Packet Radio Measurement",	COMM_IPPROTO_PRM},
		{"XEROX NS IDP",				COMM_IPPROTO_XNS_IDP},
		{"Trunk-1",						COMM_IPPROTO_TRUNK_1},
		{"Trunk-2",						COMM_IPPROTO_TRUNK_2},
		{NULL, 0}
};
/******************************************************************************************************/
/**/
/**/
/******************************************************************************************************/
#define RESOLVER_DNS_MAX_SEQID 65535
#define RESOLVER_DNS_MAX_REPLY_COUNT 32
#define RESOLVER_DNS_QUERY_SZ 512
/*******************************************************/
typedef void CommEvDNSResolverCBH(void *, void *, void *, int);
/*******************************************************/
typedef enum
{
	DNS_REPLY_TIMEDOUT,
	DNS_REPLY_SUCCESS,
	DNS_REPLY_LASTITEM
} DNSReplyCodes;
/*******************************************************/
typedef struct _DNSPendingQuery
{
	DLinkedListNode node;
	int req_count;
	int fail_count;
	int slot_id;

	struct timeval transmit_tv;
	struct timeval retransmit_tv;

	char rfc1035_request[RESOLVER_DNS_QUERY_SZ];
	unsigned long rfc1035_request_sz;

	struct
	{
		CommEvDNSResolverCBH *cb_handler;
		void *cb_data;
	} events;

	struct
	{
		unsigned int in_use:1;
		unsigned int re_xmit:1;
		unsigned int waiting_reply:1;
		unsigned int canceled_req:1;
	} flags;
} DNSPendingQuery;

typedef struct _DNSAReply
{
	int ip_count;

	struct
	{
		int ttl;
		struct in_addr addr;
	} ip_arr[RESOLVER_DNS_MAX_REPLY_COUNT];

} DNSAReply;

typedef struct _EvDNSResolverConf
{
	char *dns_ip_str;
	unsigned short dns_port;
	int lookup_timeout_ms;
	int retry_timeout_ms;
	int retry_count;
} EvDNSResolverConf;

typedef struct _EvDNSResolverBase
{
	struct _EvKQBase *ev_base;
	struct sockaddr_in dns_serv_addr;
	int socket_fd;
	int retry_count;
	int timer_id;

	struct
	{
		int retry;
		int lookup;
	} timeout_ms;

	struct
	{
		MemArena *arena;
		SlotQueue slots;
		DLinkedList req_list;
		DLinkedList reply_list;
	} pending_req;

	struct
	{
		long request_sent_sched;
		long request_sent_write;
		long request_failed;
		long request_timeout;

		long reply_received_total;
		long reply_received_valid;

		long total_bytes_sent;
		long total_bytes_received;
	} stats;

} EvDNSResolverBase;
/******************************************************************************************************/
/**/
/**/
/******************************************************************************************************/
#define ICMP_QUERY_MAX_SEQID		65535
#define ICMP_BASE_TIMEOUT_TIMER		1000
#define ICMP_BASE_MAX_TX_RETRYCOUNT	5

#define MAX_PKT_SZ					(COMM_ICMP_MAX_PAYLOAD + sizeof(struct timeval) + sizeof (char) + sizeof(CommEvICMPHeader) + 1)
/*******************************************************/
typedef void CommEvICMPBaseCBH(void*, void*, void*);
/*******************************************************/
typedef enum
{
	ICMP_CODE_TX_FAILED = -2,			/* -2 */
	ICMP_CODE_TIMEOUT 	= -1,			/* -1 */
	ICMP_CODE_ECHO_REPLY,				/* 00 */
	ICMP_CODE_UNASSIGNED_00,			/* 01 */
	ICMP_CODE_UNASSIGNED_01,			/* 02 */
	ICMP_CODE_DESTINATION_UNREACHABLE,	/* 03 */
	ICMP_CODE_SOURCE_QUENCH,			/* 04 */
	ICMP_CODE_REDIRECT,					/* 05 */
	ICMP_CODE_ALTERNATE_HOST,			/* 06 */
	ICMP_CODE_UNASSIGNED_02,			/* 07 */
	ICMP_CODE_ECHO_REQUEST,				/* 08 */
	ICMP_CODE_ROUTER_ADVERTISE,			/* 09 */
	ICMP_CODE_ROUTER_SELECTION,			/* 10 */
	ICMP_CODE_TTL_EXCEEDED,				/* 11 */
	ICMP_CODE_PARAMETER_PROBLEM,		/* 12 */
	ICMP_CODE_TIMESTAMP,				/* 13 */
	ICMP_CODE_TIMESTAMP_REPLY,			/* 14 */
	ICMP_CODE_INFORMATION_REQUEST,		/* 15 */
	ICMP_CODE_INFORMATION_REPLY,		/* 16 */
	ICMP_CODE_ADDRESS_MASK_REQUEST,		/* 17 */
	ICMP_CODE_ADDRESS_MASK_REPLY,		/* 18 */
	ICMP_CODE_UNASSIGNED_03,			/* 19 */
	ICMP_CODE_UNASSIGNED_04,			/* 20 */
	ICMP_CODE_UNASSIGNED_05,			/* 21 */
	ICMP_CODE_UNASSIGNED_06,			/* 22 */
	ICMP_CODE_UNASSIGNED_07,			/* 23 */
	ICMP_CODE_UNASSIGNED_08,			/* 24 */
	ICMP_CODE_UNASSIGNED_09,			/* 25 */
	ICMP_CODE_UNASSIGNED_10,			/* 26 */
	ICMP_CODE_UNASSIGNED_11,			/* 27 */
	ICMP_CODE_UNASSIGNED_12,			/* 28 */
	ICMP_CODE_UNASSIGNED_13,			/* 29 */
	ICMP_CODE_TRACEROUTE,				/* 30 */
	ICMP_CODE_DGRAM_CONVERSION_ERROR,	/* 31 */
	ICMP_CODE_MOBILE_HOST_REDIRECT,		/* 32 */
	ICMP_CODE_IPV6_WHERE_ARE_YOU,		/* 33 */
	ICMP_CODE_IPV6_I_AM_HERE,			/* 34 */
	ICMP_CODE_MOBILE_REGISTRATION_REQ,	/* 35 */
	ICMP_CODE_MOBILE_REGISTRATION_REPLY,/* 36 */
	ICMP_CODE_DOMAIN_NAME_REQ,			/* 37 */
	ICMP_CODE_DOMAIN_NAME_REPLY,		/* 38 */
} ICMPCodes;


typedef enum
{
	ICMP_PINGER_STATE_DISCONNECTED,
	ICMP_PINGER_STATE_RESOLVING_DNS,
	ICMP_PINGER_STATE_FAILED_DNS,
	ICMP_PINGER_STATE_LASTITEM
} ICMPPingerState;

typedef enum
{
	ICMP_EVENT_TIMEOUT,
	ICMP_EVENT_REPLY,
	ICMP_EVENT_DNS_RESOLV,
	ICMP_EVENT_DNS_FAILED,
	ICMP_EVENT_LASTITEM
} EvICMPBaseEventCodes;
/*******************************************************/
typedef struct _ICMPQueryInfo
{
	DLinkedListNode node;
	CommEvICMPBaseCBH *cb_handler;
	CommEvICMPHeader icmp_packet;
	struct timeval transmit_time;
	struct sockaddr_storage target_sockaddr;
	void *cb_data;

	int slot_id;
	int owner_id;

	int timeout_ms;
	int icmp_packet_sz;
	int tx_retry_count;

	struct
	{
		unsigned int in_use:1;
		unsigned int waiting_reply:1;
	} flags;

} ICMPQueryInfo;

typedef struct _ICMPReply
{
	CommEvIPHeader *ip_header;
	CommEvICMPHeader *icmp_packet;
	struct sockaddr_storage *sockaddr;
	int ip_header_sz;
	int ip_packet_sz;
	int icmp_header_sz;
	int icmp_payload_sz;
	int icmp_hopcount;

	unsigned int icmp_type :8;
	unsigned int icmp_code :8;
	unsigned int icmp_id :16;
	unsigned int icmp_seq :16;

} ICMPReply;

typedef struct _EvICMPPending
{
	MemArena *arena;
	DLinkedList req_list;
	DLinkedList reply_list;
	SlotQueue slots;
} EvICMPPending;

typedef struct _EvICMPBase
{
	struct _EvKQBase *ev_base;
	struct _EvKQBaseLogBase *log_base;

	int socket_fdv4;
	int socket_fdv6;

	int timer_id;
	int min_seen_timeout;

	EvICMPPending pending_v4;
	EvICMPPending pending_v6;

} EvICMPBase;

typedef struct _EvICMPPeriodicPingerConf
{
	EvDNSResolverBase *resolv_base;
	struct _EvKQBaseLogBase *log_base;
	struct sockaddr_storage *sockaddr;
	char *hostname_str;
	char *target_ip_str;
	long reset_count;
	int interval_ms;
	int timeout_ms;
	int payload_sz;
	int unique_id;
	int dns_resolv_on_fail;
	int timer_resolv_dns_ms;

} EvICMPPeriodicPingerConf;

typedef struct _EvICMPPeriodicPinger
{
	EvICMPBase *icmp_base;
	struct _EvDNSResolverBase *resolv_base;
	struct _EvKQBaseLogBase *log_base;
	int timer_id;
	int dnsreq_id;
	int state;

	struct
	{
		int resolv_dns_id;
	} timers;

	struct
	{
		int seq_reply_id;
		int seq_req_id;
		int icmp_slot_id;
	} last;

	struct
	{
		CommEvICMPBaseCBH *cb_handler_ptr;
		void *cb_data_ptr;
		struct
		{
			unsigned int enabled :1;
			unsigned int mutex_init :1;
		} flags;
	} events[ICMP_EVENT_LASTITEM];

	struct
	{
		struct sockaddr_storage sockaddr;
		char ip_addr_str[128];
		char hostname_str[1024];
		unsigned int unique_id;
		long reset_count;
		int interval;
		int timeout;
		int payload_sz;

		int timer_resolv_dns_ms;

	} cfg;

	struct
	{
		struct timeval lastreq_tv;
		struct timeval lastreply_tv;
		float packet_loss_pct;
		float latency_ms;
		long request_sent;
		long reply_recv;
		int hop_count;
	} stats;

	struct
	{
		DNSAReply a_reply;
		long expire_ts;
		int cur_idx;
	} dns;

	struct
	{
		unsigned int random_unique_id:1;
		unsigned int dns_need_lookup:1;
		unsigned int dns_balance_ips:1;
		unsigned int dns_resolv_on_fail:1;
		unsigned int destroy_after_dns_fail:1;
	} flags;

} EvICMPPeriodicPinger;
/******************************************************************************************************/
/**/
/**/
/******************************************************************************************************/
#define FTP_MAX_KNOWN_CODE 10070
/*******************************************************/
typedef void CommEvFTPClientServerActionCBH(void *, void *);
typedef void CommEvFTPClientCBH(void *, void *);
typedef void CommEvFTPClientReplyCBH(void *, void *, int);
/*******************************************************/
typedef enum
{
	EV_FTP_TRANSFER_FAILED_DATA_CONN,
	EV_FTP_TRANSFER_FAILED_FILE_NOT_ALLOWED,
	EV_FTP_TRANSFER_OK,
} CommEvFTPClientTransferCodes;

typedef enum
{
	EV_FTP_SERVICE_FILE_OK = 150,
	EV_FTP_SERVICE_READY = 220,
	EV_FTP_SERVICE_FILE_RECV_OK = 226,
	EV_FTP_SERVICE_PASSIVE = 227,
	EV_FTP_SERVICE_LOGGED_IN = 230,
	EV_FTP_SERVICE_CMD_OK = 250,
	EV_FTP_SERVICE_SENDPASS = 331,
	EV_FTP_SERVICE_CMD_FAIL = 550,
	EV_FTP_SERVICE_FILE_FAIL = 553,

} CommEvFTPClientWantedReplyCodes;

typedef enum
{
	COMM_FTPCLIENT_EVENT_AUTHFAIL,
	COMM_FTPCLIENT_EVENT_FAIL_PASSIVE_DATACONN,
	COMM_FTPCLIENT_EVENT_AUTHOK,
	COMM_FTPCLIENT_EVENT_LASTITEM,
} CommEvFTPClientEvents;

typedef enum
{
	EV_FTP_STATE_SENT_USER,
	EV_FTP_STATE_SENT_PASS,
	EV_FTP_STATE_ONLINE,
	EV_FTP_STATE_LASTITEM,
} CommEvFTPClientCurrentState;

typedef enum
{
	EV_FTP_TRANSFER_SEND,
	EV_FTP_TRANSFER_RECEIVE_MEM,
	EV_FTP_TRANSFER_RECEIVE_FILE,
	EV_FTP_TRANSFER_LASTITEM,
} CommEvFTPClientTransferDirection;

typedef enum
{
	EV_FTPCLI_CMD_ACCT,
	EV_FTPCLI_CMD_CWD,
	EV_FTPCLI_CMD_CDUP,
	EV_FTPCLI_CMD_SMNT,
	EV_FTPCLI_CMD_QUIT,
	EV_FTPCLI_CMD_REIN,
	EV_FTPCLI_CMD_PORT,
	EV_FTPCLI_CMD_PASV,
	EV_FTPCLI_CMD_TYPE,
	EV_FTPCLI_CMD_STRU,
	EV_FTPCLI_CMD_MODE,
	EV_FTPCLI_CMD_RETR,
	EV_FTPCLI_CMD_STOR,
	EV_FTPCLI_CMD_STOU,
	EV_FTPCLI_CMD_APPE,
	EV_FTPCLI_CMD_ALLO,
	EV_FTPCLI_CMD_REST,
	EV_FTPCLI_CMD_RNFR,
	EV_FTPCLI_CMD_RNTO,
	EV_FTPCLI_CMD_ABOR,
	EV_FTPCLI_CMD_DELE,
	EV_FTPCLI_CMD_RMD,
	EV_FTPCLI_CMD_MKD,
	EV_FTPCLI_CMD_PWD,
	EV_FTPCLI_CMD_LIST,
	EV_FTPCLI_CMD_NLST,
	EV_FTPCLI_CMD_SITE,
	EV_FTPCLI_CMD_SYST,
	EV_FTPCLI_CMD_STAT,
	EV_FTPCLI_CMD_HELP,
	EV_FTPCLI_CMD_NOOP,
	EV_FTPCLI_CMD_LASTITEM
} CommEvFTPClientCmdCode;
/*******************************************************/
typedef struct _EvFTPClientReplyProto
{
	int gen_code;
	int sub_code;
	char *gen_message;
	char *sub_message;
} EvFTPClientReplyProto;

typedef struct _CommEvFTPClientConfProto
{
	char *host;
	unsigned short port;
	char *username;
	char *password;

	struct
	{
		unsigned int passive :1;
	} flags;
} CommEvFTPClientConfProto;

typedef struct _CommEvFTPClientConf
{
	char host[512];
	unsigned short port;
	char username[512];
	char password[512];

	struct
	{
		unsigned int passive :1;
	} flags;

} CommEvFTPClientConf;

typedef struct _CommEvFTPClientActionCombo
{

	CommEvFTPClientServerActionCBH *action_cbh;
	void *action_cbdata;
} CommEvFTPClientActionCombo;

typedef struct _CommEvFTPClient
{
	CommEvFTPClientConf cli_conf;
	CommEvFTPClientActionCombo action_table[FTP_MAX_KNOWN_CODE];

	struct _EvKQBase *parent_ev_base;
	struct _CommEvTCPClient *ev_tcpclient_ctrl;
	struct _CommEvTCPClient *ev_tcpclient_data;
	int current_state;

	struct
	{
		CommEvFTPClientCBH *cb_handler_ptr;
		void *cb_data_ptr;
		struct
		{
			unsigned int enabled :1;
			unsigned int mutex_init :1;
		} flags;
	} events[COMM_FTPCLIENT_EVENT_LASTITEM];

	struct
	{
		CommEvFTPClientReplyCBH *cb_handler_ptr;
		void *cb_data_ptr;
		int cmd_sent;
	} cmd;

	struct
	{
		int direction;
		MemBuffer *raw_data;
		FILE *dst_file;
		CommEvFTPClientReplyCBH *cb_handler_ptr;
		void *cb_data_ptr;
	} queued_transfer;

	struct
	{
		char target_str[64];
		char target_port_str[8];
		struct in_addr target_addr;
		unsigned short target_port;
		unsigned short p1;
		unsigned short p2;
	} passive;

	struct
	{
		unsigned int busy:1;
		unsigned int passv_parse_failed:1;
		unsigned int queued_transfer:1;
		unsigned int reconnecting:1;
	} flags;



} CommEvFTPClient;

typedef struct _CommEvFTPClientCmdProto
{

	char *cmd_str;
	int cmd_size;
	int cmd_code;
	int param_count;

} CommEvFTPClientCmdProto;
/*******************************************************/
static CommEvFTPClientCmdProto ev_ftp_usercmd_arr[] =
{
		{ "ACCT", 4, EV_FTPCLI_CMD_ACCT, 1 },
		{ "CWD", 3, EV_FTPCLI_CMD_CWD, 1 },
		{ "CDUP", 4, EV_FTPCLI_CMD_CDUP, 0 },
		{ "SMNT", 4, EV_FTPCLI_CMD_SMNT, 1 },
		{ "QUIT", 4, EV_FTPCLI_CMD_QUIT, 0 },
		{ "REIN", 4, EV_FTPCLI_CMD_REIN, 0 },
		{ "PORT", 4, EV_FTPCLI_CMD_PORT, 1 },
		{ "PASV", 4, EV_FTPCLI_CMD_PASV, 0 },
		{ "TYPE", 4, EV_FTPCLI_CMD_TYPE, 1 },
		{ "STRU", 4, EV_FTPCLI_CMD_STRU, 1 },
		{ "MODE", 4, EV_FTPCLI_CMD_MODE, 1 },
		{ "RETR", 4, EV_FTPCLI_CMD_RETR, 1 },
		{ "STOR", 4, EV_FTPCLI_CMD_STOR, 1 },
		{ "STOU", 4, EV_FTPCLI_CMD_STOU, 0 },
		{ "APPE", 4, EV_FTPCLI_CMD_APPE, 1 },
		{ "ALLO", 4, EV_FTPCLI_CMD_ALLO, 1 },
		{ "REST", 4, EV_FTPCLI_CMD_REST, 1 },
		{ "RNFR", 4, EV_FTPCLI_CMD_RNFR, 1 },
		{ "RNTO", 4, EV_FTPCLI_CMD_RNTO, 1 },
		{ "ABOR", 4, EV_FTPCLI_CMD_ABOR, 0 },
		{ "DELE", 4, EV_FTPCLI_CMD_DELE, 1 },
		{ "RMD", 3, EV_FTPCLI_CMD_RMD, 1 },
		{ "MKD", 3, EV_FTPCLI_CMD_MKD, 1 },
		{ "PWD", 3, EV_FTPCLI_CMD_PWD, 0 },
		{ "LIST", 4, EV_FTPCLI_CMD_LIST, 0 },
		{ "NLST", 4, EV_FTPCLI_CMD_NLST, 0 },
		{ "SITE", 4, EV_FTPCLI_CMD_SITE, 1 },
		{ "SYST", 4, EV_FTPCLI_CMD_SYST, 0 },
		{ "STAT", 4, EV_FTPCLI_CMD_STAT, 0 },
		{ "HELP", 4, EV_FTPCLI_CMD_HELP, 0 },
		{ "NOOP", 4, EV_FTPCLI_CMD_NOOP, 0 },
		{ NULL, 0, EV_FTPCLI_CMD_LASTITEM, 0 }

};
/*******************************************************/
static EvFTPClientReplyProto ev_ftp_code_arr[] =
{
		{100, 100, "The requested action is being initiated, expect another reply before proceeding with a new command", NULL},
		{100, 110, "Restart marker replay . In this case, the text is exact and not left to the particular implementation; it must read: MARK yyyy = mmmm where yyyy is User-process data stream marker, and mmmm server's equivalent marker (note the spaces between markers and \"=\").", NULL },
		{100, 120, "Service ready in nnn minutes.", NULL},
		{100, 125, "Data connection already open; transfer starting.", NULL},
		{100, 150, "File status okay; about to open data connection.", NULL},
		{200, 200, "The requested action has been successfully completed.", NULL},
		{200, 202, "Command not implemented, superfluous at this site.", NULL},
		{200, 211, "System status, or system help reply.", NULL},
		{200, 212, "Directory status.", NULL},
		{200, 213, "File status.", NULL},
		{200, 214, "Help message.On how to use the server or the meaning of a particular non-standard command. This reply is useful only to the human user.", NULL},
		{200, 215, "NAME system type. Where NAME is an official system name from the registry kept by IANA.", NULL},
		{200, 220, "Service ready for new user.", NULL},
		{200, 221, "Service closing control connection.", NULL},
		{200, 225, "Data connection open; no transfer in progress.", NULL},
		{200, 226, "Closing data connection. Requested file action successful (for example, file transfer or file abort).", NULL},
		{200, 227, "Entering Passive Mode (h1,h2,h3,h4,p1,p2).", NULL},
		{200, 228, "Entering Long Passive Mode (long address, port).", NULL},
		{200, 229, "Entering Extended Passive Mode (|||port|).", NULL},
		{200, 230, "User logged in, proceed. Logged out if appropriate.", NULL},
		{200, 231, "User logged out; service terminated.", NULL},
		{200, 232, "Logout command noted, will complete when transfer done.", NULL},
		{200, 250, "Requested file action okay, completed.", NULL},
		{200, 257, "\"PATHNAME\" created.", NULL},
		{300, 300, "The command has been accepted, but the requested action is on hold, pending receipt of further information.", NULL},
		{300, 331, "User name okay, need password.", NULL},
		{300, 332, "Need account for login.", NULL},
		{300, 350, "Requested file action pending further information", NULL},
		{400, 400, "The command was not accepted and the requested action did not take place, but the error condition is temporary and the action may be requested again.", NULL},
		{400, 421, "Service not available, closing control connection. This may be a reply to any command if the service knows it must shut down.", NULL},
		{400, 425, "Can't open data connection.", NULL},
		{400, 426, "Connection closed; transfer aborted.", NULL},
		{400, 430, "Invalid username or password", NULL},
		{400, 434, "Requested host unavailable.", NULL},
		{400, 450, "Requested file action not taken.", NULL},
		{400, 451, "Requested action aborted. Local error in processing.", NULL},
		{400, 452, "Requested action not taken. Insufficient storage space in system.File unavailable (e.g., file busy).", NULL},
		{500, 500, "Syntax error, command unrecognized and the requested action did not take place. This may include errors such as command line too long.", NULL},
		{500, 501, "Syntax error in parameters or arguments.", NULL},
		{500, 502, "Command not implemented.", NULL},
		{500, 503, "Bad sequence of commands.", NULL},
		{500, 504, "Command not implemented for that parameter.", NULL},
		{500, 530, "Not logged in.", NULL},
		{500, 532, "Need account for storing files.", NULL},
		{500, 550, "Requested action not taken. File unavailable (e.g., file not found, no access).", NULL},
		{500, 551, "Requested action aborted. Page type unknown.", NULL},
		{500, 552, "Requested file action aborted. Exceeded storage allocation (for current directory or dataset).", NULL},
		{500, 553, "Requested action not taken. File name not allowed.", NULL},
		{600, 600, "Replies regarding confidentiality and integrity", NULL},
		{600, 631, "Integrity protected reply.", NULL},
		{600, 632, "Confidentiality and integrity protected reply.", NULL},
		{600, 633, "Confidentiality protected reply.", NULL},
		{10000, 10000, "Common Winsock Error Codes", NULL},
		{10000, 10054, "Connection reset by peer. The connection was forcibly closed by the remote host.", NULL},
		{10000, 10060, "Cannot connect to remote server.", NULL},
		{10000, 10061, "Cannot connect to remote server. The connection is actively refused by the server.", NULL},
		{10000, 10066, "Directory not empty.", NULL},
		{10000, 10068, "Too many users, server is full.", NULL}
};
/******************************************************************************************************/
/* comm/core/comm_dns_resolver.c */
/******************************************************************************************************/
EvDNSResolverBase *CommEvDNSResolverBaseNew(struct _EvKQBase *ev_base, EvDNSResolverConf *conf);
void CommEvDNSResolverBaseDestroy(EvDNSResolverBase *resolv_base);
int CommEvDNSGetHostByName(EvDNSResolverBase *resolv_base, char *host, CommEvDNSResolverCBH *cb_handler, void *cb_data);
int CommEvDNSCancelPendingRequest(EvDNSResolverBase *resolv_base, int req_id);

/******************************************************************************************************/
/* comm/core/icmp/comm_icmp_base.c */
/******************************************************************************************************/
EvICMPBase *CommEvICMPBaseNew(struct _EvKQBase *kq_base);
void CommEvICMPBaseDestroy(EvICMPBase *icmp_base);
int CommEvICMPRequestCancelByReqID(EvICMPBase *icmp_base, EvICMPPending *icmp_pending, int req_id);
int CommEvICMPRequestCancelByOwnerID(EvICMPBase *icmp_base, EvICMPPending *icmp_pending, int owner_id);

int CommEvICMPRequestSend(EvICMPBase *icmp_base, struct sockaddr_storage *sockaddr, int type, int code, int seq, int timeout_ms, char *payload, int payload_len,
		CommEvICMPBaseCBH *cb_handler, void *cb_data, int owner_id);
int CommEvICMPEchoSendRequestByInAddr(EvICMPBase *icmp_base, struct sockaddr_storage *sockaddr, int code, int seq, int size, int timeout_ms,
		CommEvICMPBaseCBH *cb_handler, void *cb_data, int owner_id);
int CommEvICMPEchoSendRequestBySockAddr(EvICMPBase *icmp_base, struct sockaddr_storage *sockaddr, int code, int seq, int size, int timeout_ms, CommEvICMPBaseCBH *cb_handler, void *cb_data, int owner_id);
int CommEvICMPEchoSendRequest(EvICMPBase *icmp_base, char *addr_str, int code, int seq, int size, int timeout_ms, CommEvICMPBaseCBH *cb_handler, void *cb_data, int owner_id);

float CommEvICMPtvSubMsec(struct timeval *when, struct timeval *now);

/* comm/utils/comm_icmp_pinger.c */
EvICMPPeriodicPinger *CommEvICMPPeriodicPingerNew(EvICMPBase *icmp_base, EvICMPPeriodicPingerConf *pinger_conf);
void CommEvICMPPeriodicPingerDestroy(EvICMPPeriodicPinger *icmp_pinger);
void CommEvICMPPeriodicPingerStatsReset(EvICMPPeriodicPinger *icmp_pinger);
int CommEvICMPPeriodicPingerEventIsSet(EvICMPPeriodicPinger *icmp_pinger, EvICMPBaseEventCodes ev_type);
void CommEvICMPPeriodicPingerEventSet(EvICMPPeriodicPinger *icmp_pinger, EvICMPBaseEventCodes ev_type, CommEvICMPBaseCBH *cb_handler, void *cb_data);
void CommEvICMPPeriodicPingerEventCancel(EvICMPPeriodicPinger *icmp_pinger, EvICMPBaseEventCodes ev_type);
void CommEvICMPPeriodicPingerEventCancelAll(EvICMPPeriodicPinger *icmp_pinger);
int CommEvICMPPeriodicPingerJSONDump(EvICMPPeriodicPinger *icmp_pinger, MemBuffer *json_reply_mb);

/******************************************************************************************************/
/* comm_ftp_client.c */
/******************************************************************************************************/
int CommEvFTPClientRetrMemBuffer(CommEvFTPClient *ev_ftp_client, MemBuffer *raw_data, char *dst_path, CommEvFTPClientReplyCBH *finish_cb, void *finish_cbdata);
int CommEvFTPClientPutMemBuffer(CommEvFTPClient *ev_ftp_client, MemBuffer *raw_data, char *dst_path, CommEvFTPClientReplyCBH *finish_cb, void *finish_cbdata);
int CommEvFTPClientCmdSend(CommEvFTPClient *ev_ftp_client, CommEvFTPClientReplyCBH *cb_handler, void *cb_data, int cmd, char *param_str);
int CommEvFTPClientEventDisable(CommEvFTPClient *ev_ftp_client, CommEvFTPClientEvents ev_type);
int CommEvFTPClientEventEnable(CommEvFTPClient *ev_ftp_client, CommEvFTPClientEvents ev_type);
int CommEvFTPClientEventCancelAll(CommEvFTPClient *ev_ftp_client);
int CommEvFTPClientEventCancel(CommEvFTPClient *ev_ftp_client, CommEvFTPClientEvents ev_type);
int CommEvFTPClientEventSet(CommEvFTPClient *ev_ftp_client, CommEvFTPClientEvents ev_type, CommEvFTPClientCBH *cb_handler, void *cb_data);
int CommEvFTPClientConnect(CommEvFTPClient *ev_ftp_client, CommEvFTPClientConfProto *ev_ftpclient_conf);
CommEvFTPClient *CommEvFTPClientNew(struct _EvKQBase *ev_base);
void CommEvFTPClientDestroy(CommEvFTPClient *ev_ftp_client);

/******************************************************************************************************/
/* comm_tftp_server.c */
/******************************************************************************************************/
CommEvTFTPServer *CommEvTFTPServerNew(struct _EvKQBase *kq_base);
int CommEvTFTPServerInit(CommEvTFTPServer *ev_tftp_server, CommEvTFTPServerConf *ev_tftp_server_conf);
void CommEvTFTPServerDestroy(CommEvTFTPServer *ev_tftp_server);
int CommEvTFTPServerErrorReply(CommEvTFTPServer *ev_tftp_server, int error_code, struct sockaddr_in *cli_addr, char *error_msg, ...);
int CommEvTFTPServerWriteMemBuffer(CommEvTFTPServer *ev_tftp_server, MemBuffer *data_mb, struct sockaddr_in *cli_addr);
void CommEvTFTPServerEventSet(CommEvTFTPServer *ev_tftp_server, CommEvTFTPEventCodes ev_type, CommEvTFTPTServerCBH *cb_handler, void *cb_data);
void CommEvTFTPServerEventCancel(CommEvTFTPServer *ev_tftp_server, CommEvTFTPEventCodes ev_type);
void CommEvTFTPServerEventCancelAll(CommEvTFTPServer *ev_tftp_server);
/******************************************************************************************************/
/* comm_tzsp_server.c */
/******************************************************************************************************/
CommEvTZSPServer *CommEvTZSPServerNew(struct _EvKQBase *kq_base);
int CommEvTZSPServerInit(CommEvTZSPServer *ev_tzsp_server, CommEvTZSPServerConf *ev_tzsp_server_conf);
void CommEvTZSPServerShutdown(CommEvTZSPServer *ev_tzsp_server);
void CommEvTZSPServerDestroy(CommEvTZSPServer *ev_tzsp_server);
void CommEvTZSPServerDissectorRegisterAll(CommEvTZSPServer *ev_tzsp_server);

CommEvTZSPLinkLayerDissector *CommEvTZSPServerLinkLayerDissectorRegister(CommEvTZSPServer *ev_tzsp_server, CommEvTZSPLinkLayerDissectorFunc *func, int encap_code, int hdr_size);
CommEvTZSPNetworkLayerDissector *CommEvTZSPServerNetworkLayerDissectorRegister(CommEvTZSPServer *ev_tzsp_server, CommEvTZSPLinkLayerDissector *ll_dissector,
		CommEvTZSPNetworkLayerDissectorFunc *func,	int proto_code, int proto_hdr_sz);
CommEvTZSPTransportLayerDissector *CommEvTZSPServerTransportLayerDissectorRegister(CommEvTZSPServer *ev_tzsp_server, CommEvTZSPNetworkLayerDissector *nl_dissector,
		CommEvTZSPTransportLayerDissectorFunc *func, int proto_code, int proto_hdr_sz);
CommEvTZSPLinkLayerDissector *CommEvTZSPServerLinkLayerDissectorByEncap(CommEvTZSPServer *ev_tzsp_server, int encap_code);
/******************************************************************************************************/


#endif /* LIBBRB_COMM_UTILS_H_ */
