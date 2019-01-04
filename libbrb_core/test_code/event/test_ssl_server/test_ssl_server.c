/*
 * test_ssl_server.c
 *
 *  Created on: 2014-01-28
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

static void mainInitService(void);

EvKQBase *glob_ev_base;
CommEvTCPServer *glob_tcp_srv;

/**************************************************************************************************************************/
void SSLServerEventsSNIParseEvent(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{

	return;

	CommEvTCPServerCertificate *ca_certinfo;
	CommEvTCPServer *ev_tcpsrv					= base_ptr;
	CommEvTCPServerConn *conn_hnd				= NULL;

	char cert_path[512];

	/* Grab a connection handler from server internal arena */
	conn_hnd 		= CommEvTCPServerConnArenaGrab(ev_tcpsrv, fd);

	/* Generate a path for this certificate */
	snprintf((char*)&cert_path, sizeof(cert_path), "./%s.x509", CommEvTCPServerConnSSLDataGetSNIStr(conn_hnd));

	/* Try to find a CACHE_CERT for this SNI */
	conn_hnd->ssldata.x509_cert = CommEvTCPServerSSLCertCacheLookupByConnHnd(conn_hnd);

	/* The is no cached CERT for this CONN, try to load from file */
	if (!conn_hnd->ssldata.x509_cert)
	{
		conn_hnd->ssldata.x509_cert = CommEvSSLUtils_X509CertFromFile((char*)&cert_path);

		/* Forge a new X.509 certificate on the fly for this connection */
		if (!conn_hnd->ssldata.x509_cert)
		{
			printf("SSLServerEventsSNIParseEvent - Forging NEW CERT for [%s]\n", CommEvTCPServerConnSSLDataGetSNIStr(conn_hnd));
			conn_hnd->ssldata.x509_cert 		= CommEvSSLUtils_X509ForgeAndSignFromConnHnd(conn_hnd, (60*60*24*3650), 1);

			/* Failed to forge CERTIFICATE, bail out */
			if (!conn_hnd->ssldata.x509_cert)
			{
				/* Close this connection */
				CommEvTCPServerConnClose(conn_hnd);
				return;
			}
		}
		else
		{
			printf("SSLServerEventsSNIParseEvent - Loaded from file\n");

		}
		/* Cache newly forged X.509 certificate for this SNI host */
		CommEvTCPServerSSLCertCacheInsert(ev_tcpsrv, CommEvTCPServerConnSSLDataGetSNIStr(conn_hnd), conn_hnd->ssldata.x509_cert);

		/* Write the certificate */
		CommEvSSLUtils_X509CertToFile(cert_path, conn_hnd->ssldata.x509_cert);

	}
	else
	{
		printf("SSLServerEventsSNIParseEvent - Using CACHED CERT for [%s]\n", CommEvTCPServerConnSSLDataGetSNIStr(conn_hnd));
	}

	//printf("SSLServerEventsSNIParseEvent - Will use CERT [%s] for TLS/VHOST OFF [%d]-[*.%s]\n", CommEvSSLUtils_X509ToStr(conn_hnd->ssldata.x509_cert),
	//		conn_hnd->ssldata.sni_host_tldpos, &conn_hnd->ssldata.sni_host_str[conn_hnd->ssldata.sni_host_tldpos]);

	return;
}
/**************************************************************************************************************************/
void SSLServerEventsAcceptEvent(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPServer *ev_tcpsrv      = base_ptr;
	CommEvTCPServerConn *conn_hnd	= NULL;

	/* Grab a connection handler from server internal arena */
	conn_hnd = CommEvTCPServerConnArenaGrab(ev_tcpsrv, fd);

	printf("SSLServerEventsAcceptEvent - FD [%d] - Accept client from IP [%s]\n", fd, conn_hnd->string_ip);

	return;
}
/**************************************************************************************************************************/
void SSLServerEventsCloseEvent(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPServer *ev_tcpsrv      = base_ptr;
	CommEvTCPServerConn *conn_hnd	= NULL;

	/* Grab a connection handler from server internal arena */
	conn_hnd = CommEvTCPServerConnArenaGrab(ev_tcpsrv, fd);

	printf("SSLServerEventsCloseEvent - FD [%d] - IP [%s] disconnected\n", fd, conn_hnd->string_ip);

	return;
}
/**************************************************************************************************************************/
void SSLServerEventsDataEvent(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPServer *tcp_srv = base_ptr;
	CommEvTCPServerConn *conn_hnd = NULL;
	char *req_str;
	int auth_status, req_size;
	int op_status;

	/* Grab a connection handler from server internal arena */
	conn_hnd = CommEvTCPServerConnArenaGrab(tcp_srv, fd);

	/* No buffer, bail out */
	if (!conn_hnd->iodata.read_buffer)
		return;

	/* Get a ptr to req_str */
	req_str		= MemBufferDeref(conn_hnd->iodata.read_buffer);
	req_size	= MemBufferGetSize(conn_hnd->iodata.read_buffer);

	MemBuffer *json_reply_mb;

	/* Create the json_mb */
	json_reply_mb = MemBufferNew( BRBDATA_THREAD_UNSAFE, 2048);

	/* Build the JSON reply */
	MEMBUFFER_JSON_BEGIN_OBJECT(json_reply_mb);

	/* Build JSON header */
	MEMBUFFER_JSON_ADD_BOOLEAN(json_reply_mb, "success", 1);

	/* Finish the JSON reply */
	MEMBUFFER_JSON_FINISH_OBJECT(json_reply_mb);

	/* Build the HTTP reply and send it back to the client */
	CommEvTCPServerConnAIOWriteAndDestroyMemBuffer(conn_hnd, json_reply_mb, NULL, NULL);

	/* Destroy temporary JSON buffer */
	MemBufferDestroy(json_reply_mb);

	printf("SSLServerEventsDataEvent - FD [%d] - IP [%s] - Data [%d]-[%.10s]\n", fd, conn_hnd->string_ip, req_size, req_str);

	/* Clean read buffer */
	MemBufferClean(conn_hnd->iodata.read_buffer);

	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
void PlainServerEventsAcceptEvent(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPServer *ev_tcpsrv      = base_ptr;
	CommEvTCPServerConn *conn_hnd	= NULL;

	/* Grab a connection handler from server internal arena */
	conn_hnd = CommEvTCPServerConnArenaGrab(ev_tcpsrv, fd);

	printf("PlainServerEventsAcceptEvent - Accept client from IP [%s]\n", conn_hnd->string_ip);

	/* Load CRYPTO_RC4 TRANSFORM */
//	EvAIOReqTransform_CryptoEnable(&conn_hnd->transform, COMM_CRYPTO_FUNC_RC4_MD5, "cryptokey", strlen("cryptokey"));

	CommEvTCPServerConnAIOWriteString(conn_hnd, "ENCRYPTED ACCEPT aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n", NULL, NULL);

	return;
}
/**************************************************************************************************************************/
void PlainServerEventsCloseEvent(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPServer *ev_tcpsrv      = base_ptr;
	CommEvTCPServerConn *conn_hnd	= NULL;

	/* Grab a connection handler from server internal arena */
	conn_hnd = CommEvTCPServerConnArenaGrab(ev_tcpsrv, fd);

	printf("PlainServerEventsCloseEvent - IP [%s] disconnected\n", conn_hnd->string_ip);

	return;
}
/**************************************************************************************************************************/
void PlainServerEventsDataEvent(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPServer *tcp_srv = base_ptr;
	CommEvTCPServerConn *conn_hnd = NULL;
	MemBuffer *html_mb;
	char *req_str;
	int auth_status, req_size;
	int op_status;

	/* Grab a connection handler from server internal arena */
	conn_hnd = CommEvTCPServerConnArenaGrab(tcp_srv, fd);

	/* No buffer, bail out */
	if (!conn_hnd->iodata.read_buffer)
		return;

	/* Get a ptr to req_str */
	req_str		= MemBufferDeref(conn_hnd->iodata.read_buffer);
	req_size	= MemBufferGetSize(conn_hnd->iodata.read_buffer);

	//html_mb = MemBufferNew(BRBDATA_THREAD_UNSAFE, 1024);

	//WebEngineBaseJSONSendEmptySuccessReply(conn_hnd);
	CommEvTCPServerConnAIOWriteStringFmt(conn_hnd, NULL, NULL, "ENCRYPTED DATA [%ld]\n", arc4random());

	printf("PlainServerEventsDataEvent - IP [%s] - Data [%d]-[%s]\n", conn_hnd->string_ip, req_size, req_str);

	/* Clean read buffer */
	MemBufferClean(conn_hnd->iodata.read_buffer);

	return;
}
/**************************************************************************************************************************/
int main(void)
{
	BRB_RC4_State rc4_state_enc;
	BRB_RC4_State rc4_state_dec;

	int kq_retcode;

//	char *buf_orig = "encrypted";
//	char *buf_enc = calloc(1, 1000);
//	char *buf_dec = calloc(1, 1000);
//
//
//	memset(&rc4_state_enc, 0, sizeof(BRB_RC4_State));
//	memset(&rc4_state_dec, 0, sizeof(BRB_RC4_State));
//
//	BRB_RC4_Init(&rc4_state_enc, (unsigned char*)"abcd", 4);
//	BRB_RC4_Init(&rc4_state_dec, (unsigned char*)"abcd", 4);
//
//	BRB_RC4_Crypt(&rc4_state_enc, (unsigned char*)buf_orig, buf_enc, strlen(buf_orig));
//	BRB_RC4_Crypt(&rc4_state_dec, (unsigned char*)buf_enc, buf_dec, strlen(buf_orig));
//
//	printf("ORIG [%s] - ENC [%s] - DEC [%s]\n", buf_orig, buf_enc, buf_dec);
//
//
//	return 1;

	/* Initialize KQEvent */
	glob_ev_base = EvKQBaseNew(NULL);

	mainInitService();

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, 30);

	printf("Terminating...!\n");

	exit(0);

}
/**************************************************************************************************************************/
static void mainInitService(void)
{
	CommEvTCPServerConf conf_plain;
	CommEvTCPServerConf conf_ssl;
	int plain_listener_id;
	int ssl_listener_id;

	/* Create and launch server */
	glob_tcp_srv	= CommEvTCPServerNew(glob_ev_base);

	memset(&conf_plain, 0, sizeof(CommEvTCPServerConf));
	conf_plain.bind_method	= COMM_SERVER_BINDANY;
	conf_plain.read_mthd	= COMM_SERVER_READ_MEMBUFFER;
	conf_plain.srv_proto	= COMM_SERVERPROTO_PLAIN;
	conf_plain.port			= 999;

	conf_plain.flags.reuse_addr		= 1;
	conf_plain.flags.reuse_port		= 1;

	/* Configure self synchronize parameters */
	conf_plain.self_sync.token_str		= "\n";
	conf_plain.self_sync.max_buffer_sz	= 8092;

	conf_plain.events[COMM_SERVER_EVENT_ACCEPT_AFTER].handler	= PlainServerEventsAcceptEvent;
	conf_plain.events[COMM_SERVER_EVENT_DEFAULT_READ].handler	= PlainServerEventsDataEvent;
	conf_plain.events[COMM_SERVER_EVENT_DEFAULT_CLOSE].handler	= PlainServerEventsCloseEvent;
	conf_plain.events[COMM_SERVER_EVENT_ACCEPT_AFTER].data		= NULL;
	conf_plain.events[COMM_SERVER_EVENT_DEFAULT_READ].data		= NULL;
	conf_plain.events[COMM_SERVER_EVENT_DEFAULT_CLOSE].data		= NULL;

	memset(&conf_ssl, 0, sizeof(CommEvTCPServerConf));

	conf_ssl.bind_method	= COMM_SERVER_BINDANY;
	conf_ssl.read_mthd		= COMM_SERVER_READ_MEMBUFFER;
	conf_ssl.srv_proto		= COMM_SERVERPROTO_AUTODETECT;

	conf_ssl.timeout.autodetect_ms = 1000;

	conf_ssl.ssl.ca_cert_path	= "./server.crt";
	conf_ssl.ssl.ca_key_path	= "./server.key";
	conf_ssl.port				= 3322;

	conf_ssl.events[COMM_SERVER_EVENT_ACCEPT_AFTER].handler		= SSLServerEventsAcceptEvent;
	conf_ssl.events[COMM_SERVER_EVENT_ACCEPT_SNIPARSE].handler	= SSLServerEventsSNIParseEvent;
	conf_ssl.events[COMM_SERVER_EVENT_DEFAULT_READ].handler		= SSLServerEventsDataEvent;
	conf_ssl.events[COMM_SERVER_EVENT_DEFAULT_CLOSE].handler	= SSLServerEventsCloseEvent;
	conf_ssl.events[COMM_SERVER_EVENT_ACCEPT_AFTER].data		= NULL;
	conf_ssl.events[COMM_SERVER_EVENT_DEFAULT_READ].data		= NULL;
	conf_ssl.events[COMM_SERVER_EVENT_DEFAULT_CLOSE].data		= NULL;

	glob_tcp_srv->ssldata.main_key = CommEvSSLUtils_X509PrivateKeyReadFromFile("./test.pkey");

	plain_listener_id	= CommEvTCPServerListenerAdd(glob_tcp_srv, &conf_plain);
	ssl_listener_id		= CommEvTCPServerListenerAdd(glob_tcp_srv, &conf_ssl);

	//	/* Set default accept and data events */
	//	CommEvTCPServerSetEvent(glob_tcp_srv, plain_listener_id, COMM_SERVER_EVENT_ACCEPT_AFTER, PlainServerEventsAcceptEvent, NULL);
	//	CommEvTCPServerSetEvent(glob_tcp_srv, plain_listener_id, COMM_SERVER_EVENT_DEFAULT_READ, PlainServerEventsDataEvent, NULL);
	//	CommEvTCPServerSetEvent(glob_tcp_srv, plain_listener_id, COMM_SERVER_EVENT_DEFAULT_CLOSE, PlainServerEventsCloseEvent, NULL);
	//
	//	/* Set default accept and data events */
	//	CommEvTCPServerSetEvent(glob_tcp_srv, ssl_listener_id, COMM_SERVER_EVENT_ACCEPT_AFTER, SSLServerEventsAcceptEvent, NULL);
	//	CommEvTCPServerSetEvent(glob_tcp_srv, ssl_listener_id, COMM_SERVER_EVENT_DEFAULT_READ, SSLServerEventsDataEvent, NULL);
	//	CommEvTCPServerSetEvent(glob_tcp_srv, ssl_listener_id, COMM_SERVER_EVENT_DEFAULT_CLOSE, SSLServerEventsCloseEvent, NULL);

	/* Check if it went ok */
	if (plain_listener_id < 0)
	{
		printf("mainInitService - Server FAILED to initialize with CODE [%d]\n", plain_listener_id);
	}
	else
	{
		printf("mainInitService - Server initialized PLAIN ID [%d] and SSL ID [%d]\n", plain_listener_id, ssl_listener_id);


	}

	return;
}
