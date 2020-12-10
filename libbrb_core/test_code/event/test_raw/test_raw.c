/*
 * test_raw.c
 *
 *  Created on: 2016-04-12
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

#include <libbrb_core.h>
#include <net/if_dl.h>

static EvBaseKQCBH CommEvRAWEventRead;
static EvBaseKQCBH CommEvRAWEventWrite;

EvKQBase *glob_ev_base;
EvKQBaseLogBase *glob_log_base;
int glob_rawfd;
struct sockaddr_dl glob_device_addr;

uint8_t lldp_ttl_big[] =  "\
\x04\x7d\x7b\xa8\xf0\xb9\x00\xa0\x12\x22\x22\x22\x88\xcc\x02\x07\
\x04\x00\xa0\x12\x1b\x00\xa6\x04\x07\x03\x00\xa0\x12\x1b\x00\xa8\
\x06\x02\xff\xff\x10\x0c\x05\x01\x0a\x03\xd9\xd9\x02\x00\x00\x00\
\x02\x00\x10\x0c\x05\x01\x0b\x00\x00\x01\x02\x00\x00\x00\x03\x00\
\x10\x0c\x05\x01\x7f\x00\x00\x01\x02\x00\x00\x00\x01\x00\x10\x0e\
\x07\x06\x00\xa0\x12\x1b\x00\xa6\x02\x00\x00\x00\x00\x00\x08\x14\
\x50\x68\x79\x73\x69\x63\x61\x6c\x20\x49\x6e\x74\x65\x72\x66\x61\
\x63\x65\x20\x32\x0c\x44\x4e\x4f\x4b\x49\x41\x20\x45\x53\x42\x32\
\x36\x20\x73\x6f\x66\x74\x77\x61\x72\x65\x20\x76\x65\x72\x73\x69\
\x6f\x6e\x20\x34\x2e\x34\x2e\x30\x20\x62\x75\x69\x6c\x64\x20\x30\
\x33\x20\x20\x20\x4e\x6f\x76\x20\x32\x39\x20\x32\x30\x30\x36\x20\
\x2d\x20\x31\x37\x3a\x30\x39\x3a\x30\x34\x0e\x04\x00\x12\x00\x12\
\x00\x00";

/**************************************************************************************************************************/
int main(int argc, char **argv)
{
	EvICMPPeriodicPingerConf pinger_conf;
	EvKQBaseLogBaseConf log_conf;
	int op_status;

	if (argc != 2)
	{
		printf("Usage: %s <ifname>\n", argv[0]);
		exit(0);
	}

	bzero(&glob_device_addr, sizeof(glob_device_addr));
	glob_device_addr.sdl_index 		= if_nametoindex(argv[1]);

	if (glob_device_addr.sdl_index == 0)
	{
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Failed to obtain interface %s ifindex", argv[1]);
		exit(0);
	}

	/* Fill in the device address */
	glob_device_addr.sdl_family 	= AF_LINK;
	memcpy (glob_device_addr.sdl_data, lldp_ttl_big + ETHER_ADDR_LEN, ETHER_ADDR_LEN);
	glob_device_addr.sdl_alen 		= htons(ETHER_ADDR_LEN);

	memset(&log_conf, 0, sizeof(EvKQBaseLogBaseConf));
	log_conf.flags.double_write		= 1;
	log_conf.flags.debug_disable 	= 0;

	/* Create event base */
	glob_ev_base					= EvKQBaseNew(NULL);
	glob_log_base					= EvKQBaseLogBaseNew(glob_ev_base, &log_conf);

	/* Create a new RAW socket and set it to non_blocking */
//	glob_rawfd 						= EvKQBaseSocketRAWNew(glob_ev_base, AF_INET, htons(0x0003));
	glob_rawfd 						= EvKQBaseSocketRAWNew(glob_ev_base, AF_LINK, IPPROTO_RAW);
//	glob_rawfd 						= EvKQBaseSocketRawNewAndBind(glob_ev_base);
	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "TEST_RAW begin on PID [%d] - FD [%d]\n", getpid(), glob_rawfd);

	const int on = 1;
    struct ifreq ethreq;

//    // set network card to promiscuos
//    strncpy(ethreq.ifr_name, "bge0", IFNAMSIZ);
//    if (ioctl(glob_rawfd, SIOCGIFFLAGS, &ethreq) == -1)
//    {
//    	printf ("Error setting SIOCGIFFLAGS. errno [%d] - [%s]\n", errno, strerror(errno));
//        close(glob_rawfd);
//        exit(1);
//    }
//
//    ethreq.ifr_flags 	|= IFF_PROMISC;
//    if (ioctl(glob_rawfd, SIOCSIFFLAGS, &ethreq) == -1)
//    {
//    	printf ("Error setting SIOCSIFFLAGS. errno [%d] - [%s]\n", errno, strerror(errno));
//        close(glob_rawfd);
//        exit(1);
//    }


	/* Set socket to REUSE_ADDR flag */
	op_status = EvKQBaseSocketSetNonBlock(glob_ev_base, glob_rawfd);

	/* Failed setting flag */
	if (op_status < 0)
	{
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed setting NONBLOCK on FD [%d] - ERRNO [%d / %s]\n", glob_rawfd, errno, strerror(errno));
		close(glob_rawfd);
		return -2;
	}

//	if (setsockopt (glob_rawfd, IPPROTO_IP, IP_HDRINCL, &on, sizeof (on)) < 0)
//	{
//		printf ("Error setting IP_HDRINCL. errno [%d] - [%s]\n", errno, strerror(errno));
//		exit(0);
//	}

	EvKQBaseSetEvent(glob_ev_base, glob_rawfd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvRAWEventRead, NULL);
	EvKQBaseSetEvent(glob_ev_base, glob_rawfd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvRAWEventWrite, NULL);

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, 100);

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvRAWEventRead(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base 	= base_ptr;
	char buffer_ptr[65535];
	int read_bytes;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "READ_EV [%d]\n", to_read_sz);
	EvKQBaseSetEvent(glob_ev_base, glob_rawfd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvRAWEventRead, NULL);

	read_bytes 			= read(fd, (char *)&buffer_ptr, sizeof(buffer_ptr) - 1);

	if (read_bytes < 0)
		return 0;

	EvKQBaseLoggerHexDump(glob_log_base, LOGTYPE_DEBUG, (char *)&buffer_ptr, read_bytes, 8, 4);

	return 0;
}
/**************************************************************************************************************************/
static int CommEvRAWEventWrite(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base 	= base_ptr;
	int wrote_bytes;


	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "WRITE_EV [%d]\n", to_read_sz);
	EvKQBaseSetEvent(glob_ev_base, glob_rawfd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvRAWEventWrite, NULL);

	wrote_bytes 	= sendto(fd, lldp_ttl_big, sizeof(lldp_ttl_big), 0, (struct sockaddr *) &glob_device_addr, sizeof (glob_device_addr));

	if (wrote_bytes <= 0)
	{
		printf ("Error WRITE. errno [%d] - [%s]\n", errno, strerror(errno));
		exit(0);
	}
	return 1;
}
/**************************************************************************************************************************/
