/*
 * libbrb_comm_proto.h
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

#ifndef LIBBRB_COMM_PROTO_H_
#define LIBBRB_COMM_PROTO_H_

#define COMM_ICMP_MAX_PAYLOAD							8092
#define COMM_ETHERNET_FRAME_SIZE						14
#define COMM_ETHVLAN_FRAME_SIZE							4

#define COMM_IPV4_HEADER_SIZE							20
#define COMM_IPV6_HEADER_SIZE							40

#define COMM_SSL_SNI_MAXSZ								256


#define COMM_SOCK_ADDR_PTR(ptr)	((struct sockaddr *)(ptr))
#define COMM_SOCK_ADDR_FAMILY(ptr)	COMM_SOCK_ADDR_PTR(ptr)->sa_family
#ifdef HAS_SA_LEN
#define COMM_SOCK_ADDR_LEN(ptr)	COMM_SOCK_ADDR_PTR(ptr)->sa_len
#endif

#define COMM_SOCK_ADDR_IN_PTR(sa)	((struct sockaddr_in *)(sa))
#define COMM_SOCK_ADDR_IN_FAMILY(sa)	COMM_SOCK_ADDR_IN_PTR(sa)->sin_family
#define COMM_SOCK_ADDR_IN_PORT(sa)	COMM_SOCK_ADDR_IN_PTR(sa)->sin_port
#define COMM_SOCK_ADDR_IN_ADDR(sa)	COMM_SOCK_ADDR_IN_PTR(sa)->sin_addr
#define IN_ADDR(ia)		(*((struct in_addr *) (ia)))

#ifndef HAS_SA_LEN
#define COMM_SOCK_ADDR_LEN(sa) (COMM_SOCK_ADDR_PTR(sa)->sa_family == AF_INET6 ? \
		sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in))
#endif

#define COMM_SOCK_ADDR_PORT(sa) \
    (COMM_SOCK_ADDR_PTR(sa)->sa_family == AF_INET6 ? \
	COMM_SOCK_ADDR_IN6_PORT(sa) : COMM_SOCK_ADDR_IN_PORT(sa))
#define COMM_SOCK_ADDR_PORTP(sa) \
    (COMM_SOCK_ADDR_PTR(sa)->sa_family == AF_INET6 ? \
	&COMM_SOCK_ADDR_IN6_PORT(sa) : &COMM_SOCK_ADDR_IN_PORT(sa))

#define COMM_SOCK_ADDR_IN6_PTR(sa)	((struct sockaddr_in6 *)(sa))
#define COMM_SOCK_ADDR_IN6_FAMILY(sa) COMM_SOCK_ADDR_IN6_PTR(sa)->sin6_family
#define COMM_SOCK_ADDR_IN6_PORT(sa)	COMM_SOCK_ADDR_IN6_PTR(sa)->sin6_port
#define COMM_SOCK_ADDR_IN6_ADDR(sa)	COMM_SOCK_ADDR_IN6_PTR(sa)->sin6_addr
#define IN6_ADDR(ia)		(*((struct in6_addr *) (ia)))

#define COMM_SOCK_ADDR_EQ_ADDR(sa, sb) \
    ((COMM_SOCK_ADDR_FAMILY(sa) == AF_INET && COMM_SOCK_ADDR_FAMILY(sb) == AF_INET \
      && COMM_SOCK_ADDR_IN_ADDR(sa).s_addr == COMM_SOCK_ADDR_IN_ADDR(sb).s_addr) \
     || (COMM_SOCK_ADDR_FAMILY(sa) == AF_INET6 && COMM_SOCK_ADDR_FAMILY(sb) == AF_INET6 \
         && memcmp((char *) &(COMM_SOCK_ADDR_IN6_ADDR(sa)), \
                   (char *) &(COMM_SOCK_ADDR_IN6_ADDR(sb)), \
                   sizeof(COMM_SOCK_ADDR_IN6_ADDR(sa))) == 0))

#define COMM_SOCK_ADDR_EQ_PORT(sa, sb) \
    ((COMM_SOCK_ADDR_FAMILY(sa) == AF_INET && COMM_SOCK_ADDR_FAMILY(sb) == AF_INET \
      && COMM_SOCK_ADDR_IN_PORT(sa) == COMM_SOCK_ADDR_IN_PORT(sb)) \
     || (COMM_SOCK_ADDR_FAMILY(sa) == AF_INET6 && COMM_SOCK_ADDR_FAMILY(sb) == AF_INET6 \
         && COMM_SOCK_ADDR_IN6_PORT(sa) == COMM_SOCK_ADDR_IN6_PORT(sb)))

/******************************************************************************************************/
/**/
/**/
/******************************************************************************************************/
typedef enum
{
	COMM_ETHERNET_ENCAP_IPV4	= 0x0800,
	COMM_ETHERNET_ENCAP_ARP		= 0x0806,
	COMM_ETHERNET_ENCAP_WOL		= 0x0842,
	COMM_ETHERNET_ENCAP_AVTP	= 0x22F0, /* Audio Video Transport Protocol IEEE std 1722-2011 */
	COMM_ETHERNET_ENCAP_RARP	= 0x8035,
	COMM_ETHERNET_ENCAP_VLAN	= 0x8100,
	COMM_ETHERNET_ENCAP_IPX0	= 0x8137,
	COMM_ETHERNET_ENCAP_IPX1	= 0x8138,
	COMM_ETHERNET_ENCAP_IPV6	= 0x86DD,
	COMM_ETHERNET_ENCAP_EFC		= 0x8808, /* Ethernet Flow Control */
	COMM_ETHERNET_ENCAP_MPLS_U	= 0x8847, /* MPLS UNICAST */
	COMM_ETHERNET_ENCAP_MPLS_M	= 0x8848, /* MPLS MULTICAST */
	COMM_ETHERNET_ENCAP_PPPOE_D	= 0x8863, /* PPPoE Discovery */
	COMM_ETHERNET_ENCAP_PPPOE_S	= 0x8864, /* PPPoE Session */
	COMM_ETHERNET_ENCAP_JUMBO	= 0x8870, /* JUMBO Frames */
	COMM_ETHERNET_ENCAP_ATAOE	= 0x88A2, /* ATA over Ethernet */
	COMM_ETHERNET_ENCAP_LLDP	= 0x88A2, /* Link Layer Discovery Protocol */
	COMM_ETHERNET_ENCAP_MACSEC	= 0x88E5, /* MAC security - IEEE 802.1AE */
} CommEthernetEncapCodes;

typedef enum
{
	COMM_IPPROTO_HOPOPT,		 	/* IPv6 Hop-by-Hop Option */
	COMM_IPPROTO_ICMP,				/* Internet Control Message Protocol */
	COMM_IPPROTO_IGMP,				/* Internet Group Management Protocol */
	COMM_IPPROTO_GGP,				/* Gateway-to-Gateway Protocol */
	COMM_IPPROTO_IP_IP,				/* IP in IP (encapsulation) */
	COMM_IPPROTO_ST,				/* Internet Stream Protocol */
	COMM_IPPROTO_TCP,				/* Transmission Control Protocol */
	COMM_IPPROTO_CBT,				/* Core-based trees */
	COMM_IPPROTO_EGP,				/* Exterior Gateway Protocol */
	COMM_IPPROTO_IGP,				/* Interior Gateway Protocol (any private interior gateway (used by Cisco for their IGRP)) */
	COMM_IPPROTO_BBN_RCC_MON,		/* BBN RCC Monitoring */
	COMM_IPPROTO_NVP_II,			/* Network Voice Protocol */
	COMM_IPPROTO_PUP,				/* Xerox PUP */
	COMM_IPPROTO_ARGUS,				/* ARGUS */
	COMM_IPPROTO_EMCON,				/* EMCON */
	COMM_IPPROTO_XNET,				/* Cross Net Debugger */
	COMM_IPPROTO_CHAOS,				/* Chaos */
	COMM_IPPROTO_UDP,				/* User Datagram Protocol */
	COMM_IPPROTO_MUX,				/* Multiplexing */
	COMM_IPPROTO_DCN_MEAS,			/* DCN Measurement Subsystems */
	COMM_IPPROTO_HMP,				/* Host Monitoring Protocol */
	COMM_IPPROTO_PRM,				/* Packet Radio Measurement */
	COMM_IPPROTO_XNS_IDP,			/* XEROX NS IDP */
	COMM_IPPROTO_TRUNK_1,			/* Trunk-1 */
	COMM_IPPROTO_TRUNK_2,			/* Trunk-2 */
	COMM_IPPROTO_LASTITEM
} CommIPProtoCodes;

#define COMM_TCPFLAG_FIN 	0x0001
#define COMM_TCPFLAG_SYN 	0x0002
#define COMM_TCPFLAG_RST 	0x0004
#define COMM_TCPFLAG_PSH 	0x0008
#define COMM_TCPFLAG_ACK 	0x0010
#define COMM_TCPFLAG_URG 	0x0020
#define COMM_TCPFLAG_ECN 	0x0040
#define COMM_TCPFLAG_CWR 	0x0080
#define COMM_TCPFLAG_NS 	0x0100
#define COMM_TCPFLAG_RES 	0x0E00 /* 3 reserved bits */
#define COMM_TCPFLAG_MASK 	0x0FFF

//typedef enum
//{
//	COMM_TCPFLAG_FIN 	= 0x0001,
//	COMM_TCPFLAG_SYN 	= 0x0002,
//	COMM_TCPFLAG_RST 	= 0x0004,
//	COMM_TCPFLAG_PSH 	= 0x0008,
//	COMM_TCPFLAG_ACK 	= 0x0010,
//	COMM_TCPFLAG_URG 	= 0x0020,
//	COMM_TCPFLAG_ECN 	= 0x0040,
//	COMM_TCPFLAG_CWR 	= 0x0080,
//	COMM_TCPFLAG_NS 	= 0x0100,
//	COMM_TCPFLAG_RES 	= 0x0E00, /* 3 reserved bits */
//	COMM_TCPFLAG_MASK 	= 0x0FFF,
//} CommTCPFlagCodes;


#define IS_TCP_TH_FIN(x) (x & COMM_TCPFLAG_FIN)
#define IS_TCP_TH_URG(x) (x & COMM_TCPFLAG_URG)

/******************************************************************************************************/
/**/
/**/
/******************************************************************************************************/
typedef enum
{
	COMM_TCP_AIO_UNINITIALIZED,
	COMM_TCP_AIO_READ_NEEDED,
	COMM_TCP_AIO_READ_FINISHED,
	COMM_TCP_AIO_READ_ERR_FATAL,
	COMM_TCP_AIO_WRITE_NEEDED,
	COMM_TCP_AIO_WRITE_FINISHED,
	COMM_TCP_AIO_WRITE_ERR_FATAL,
	COMM_TCP_AIO_LASTITEM
} CommTCPAioFinishCodes;
/******************************************************************************************************/
typedef enum
{
	COMM_CURRENT,
	COMM_PREVIOUS,
	COMM_LASTITEM
} CommEvWhenStatsCodes;
/******************************************************************************************************/
/**/
/**/
/******************************************************************************************************/
typedef struct _CommEvEthernetHeader
{
	unsigned char mac_dst[6];
	unsigned char mac_src[6];
	unsigned short type;
} CommEvEthernetHeader;

typedef struct _CommEvIPHeader
{
	unsigned int ip_vhl:4;		/* Length of the header in dwords */
	unsigned int version:4;		/* Version of IP                  */
	unsigned int tos:8;			/* Type of service                */
	unsigned int total_len:16; 	/* Length of the packet in dwords */
	unsigned int ident:16;		/* unique identifier              */
	unsigned int flags:16;		/* Flags                          */
	unsigned int ip_ttl:8;		/* Time to live                   */
	unsigned int proto:8;		/* Protocol number (TCP, UDP etc) */
	unsigned int checksum:16;	/* IP checksum                    */
	struct in_addr source_ip;
	struct in_addr dest_ip;
} CommEvIPHeader;

typedef struct _CommEvUDPHeader
{
	unsigned int port_src:16; 	/* The source port */
	unsigned int port_dst:16; 	/* The source port */
	unsigned int len:16; 		/* The packet length */
	unsigned int checksum:16; 	/* The UDP checksum */
} CommEvUDPHeader;

//typedef struct _CommEvIPV6Header {
//	unsigned char priority:4;
//	unsigned char version:4;
//	unsigned char flow_lbl[3];
//	unsigned short payload_len;
//	unsigned char nexthdr;
//	unsigned char hop_limit;
//    struct  in6_addr source_ip;
//    struct  in6_addr dest_ip;
//} CommEvIPV6Header;

typedef struct _CommEvTCPHeader
{
	unsigned int src_port:16;
	unsigned int dst_port:16;
	unsigned int seq:32;
	unsigned int ack:32;
	unsigned int hdr_len:4;
	unsigned int flags:12;
	unsigned int window_size:16;
	unsigned int checksum:16;
	unsigned int urgent:16;
} CommEvTCPHeader;

typedef struct _CommEvICMPHeader
{
	unsigned int icmp_type :8;
	unsigned int icmp_code :8;
	unsigned int icmp_cksum :16;
	unsigned int icmp_id :16;
	unsigned int icmp_seq :16;
	/* Not part of ICMP, but we need it */
	char payload[COMM_ICMP_MAX_PAYLOAD];
} CommEvICMPHeader;
/******************************************************************************************************/
/**/
/**/
/******************************************************************************************************/
typedef struct _CommEvToken
{
	char *token_str;
	int token_data;
} CommEvToken;

typedef struct _CommEvStatistics
{
	struct _EvKQBase *ev_base;
	struct timeval last_read_tv;
	struct timeval last_write_tv;
	struct timeval last_user_tv;
	unsigned long last_read_ts;
	unsigned long last_write_ts;
	unsigned long last_user_ts;

	struct
	{
		long byte_rx;
		long byte_tx;
		long ssl_byte_rx;
		long ssl_byte_tx;
		long packet_rx;
		long packet_tx;
		long user00;
		long user01;
		long user02;
		long user03;
	} total[COMM_LASTITEM];

	struct
	{
		float byte_rx;
		float byte_tx;
		float ssl_byte_rx;
		float ssl_byte_tx;
		float packet_rx;
		float packet_tx;
		float user00;
		float user01;
		float user02;
		float user03;
	} rate;

} CommEvStatistics;

typedef struct _CommEvTCPIOData
{
	pthread_mutex_t mutex;
	MemStream *read_stream;
	MemBuffer *read_buffer;
	MemStream *partial_read_stream;
	MemBuffer *partial_read_buffer;
	EvAIOReqQueue write_queue;
	int ref_count;
} CommEvTCPIOData;

typedef struct _CommEvTCPIOResult
{
	long aio_total_sz;
	int aio_count;
} CommEvTCPIOResult;

typedef struct _CommEvTCPSSLData
{
	SSL_CTX *ssl_context;
	SSL *ssl_handle;
	X509 *x509_cert;
	char sni_host_str[COMM_SSL_SNI_MAXSZ];
	char *sni_host_ptr;
	int ssl_shutdown_trycount;
	int ssl_negotiatie_trycount;
	int sni_parse_trycount;
	int sni_host_strsz;
	int sni_host_tldpos;
	int x509_forge_reqid;
	int shutdown_jobid;
} CommEvTCPSSLData;



/******************************************************************************************************/
/**/
/**/
/******************************************************************************************************/

#endif /* LIBBRB_COMM_PROTO_H_ */
