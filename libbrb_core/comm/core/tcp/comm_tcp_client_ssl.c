/*
 * comm_tcp_client_ssl.c
 *
 *  Created on: 2012-01-11
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

static EvBaseKQJobCBH CommEvTCPClientSSLShutdownJob;

/**************************************************************************************************************************/
int CommEvTCPClientSSLShutdownBegin(CommEvTCPClient *ev_tcpclient)
{
	/* Already shutting down, bail out */
	if (ev_tcpclient->flags.ssl_shuting_down)
		return 0;

	/* Set flags as shutting down */
	ev_tcpclient->flags.ssl_shuting_down = 1;

	/* Schedule SSL shutdown JOB for NEXT IO LOOP */
	ev_tcpclient->ssldata.shutdown_jobid = EvKQJobsAdd(ev_tcpclient->kq_base, JOB_ACTION_ADD_VOLATILE, 0, CommEvTCPClientSSLShutdownJob, ev_tcpclient);

	return 1;
}
/**************************************************************************************************************************/
int CommEvTCPClientSSLDataInit(CommEvTCPClient *ev_tcpclient)
{
	int op_status;

	if (COMM_CLIENTPROTO_SSL != ev_tcpclient->cli_proto)
		return COMM_CLIENT_INIT_OK;

	/* TODO: User MUST can set client method */
	if (!ev_tcpclient->ssldata.ssl_context)
		ev_tcpclient->ssldata.ssl_context	= SSL_CTX_new(SSLv23_client_method());

	/* Failed creating SSL context */
	if (!ev_tcpclient->ssldata.ssl_context)
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Failed creating SSL_CONTEXT\n", ev_tcpclient->socket_fd);
		return COMM_CLIENT_FAILURE_SSL_CONTEXT;
	}

	/* NULL CYPHER asked */
	if (ev_tcpclient->flags.ssl_null_cypher)
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_GREEN, "FD [%d] - Using NULL CYPHER SSL\n", ev_tcpclient->socket_fd);

		if (!SSL_CTX_set_cipher_list(ev_tcpclient->ssldata.ssl_context, "aNULL"))
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Failed setting NULL CIPHER\n", ev_tcpclient->socket_fd);
			return COMM_CLIENT_FAILURE_SSL_CIPHER;
		}
	}

	/* Has configuration, need to be done after context and before new */
	/* Use client certificate */
	if (ev_tcpclient->cfg.ssl.path_crt[0] != '\0' && ev_tcpclient->cfg.ssl.path_key[0] != '\0')
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_DEBUG, LOGCOLOR_BLUE, "FD [%d] - LOAD CERT [%s]-[%s]\n", ev_tcpclient->socket_fd,
				(char *)&ev_tcpclient->cfg.ssl.path_crt, (char *)&ev_tcpclient->cfg.ssl.path_key);

		/* Load the CERTIFICATE FILE into the SSL_CTX structure */
		op_status 		= SSL_CTX_use_certificate_file(ev_tcpclient->ssldata.ssl_context, (char *)&ev_tcpclient->cfg.ssl.path_crt, SSL_FILETYPE_PEM);

		if (op_status <= 0)
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Failure to use certificate [%s]\n", ev_tcpclient->socket_fd, (char *)&ev_tcpclient->cfg.ssl.path_crt);
			return COMM_CLIENT_FAILURE_SSL_CERT;
		}

		/* Load the KEY FILE into the SSL_CTX structure */
		op_status 		= SSL_CTX_use_PrivateKey_file(ev_tcpclient->ssldata.ssl_context, (char *)&ev_tcpclient->cfg.ssl.path_key, SSL_FILETYPE_PEM);

		if (op_status <= 0)
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Failure to use private [%s]\n", ev_tcpclient->socket_fd, (char *)&ev_tcpclient->cfg.ssl.path_key);
			return COMM_CLIENT_FAILURE_SSL_KEY;
		}

		/* Check private key consistency */
		op_status 		= SSL_CTX_check_private_key(ev_tcpclient->ssldata.ssl_context);

		if (!op_status)
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Private key does not match the public certificate\n", ev_tcpclient->socket_fd);
			return COMM_CLIENT_FAILURE_SSL_MATCH;
		}
	}

	if (ev_tcpclient->cfg.ssl.path_ca[0] != '\0')
	{
		/* Load CA certificate */
		op_status 		= SSL_CTX_load_verify_locations(ev_tcpclient->ssldata.ssl_context, (char *)&ev_tcpclient->cfg.ssl.path_ca, NULL);

		if (!op_status)
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - verify location Failed - OP [%d]\n", ev_tcpclient->socket_fd, op_status);
			return COMM_CLIENT_FAILURE_SSL_CA;
		}

		/* Require verification of the server's certificate by the client */
		SSL_CTX_set_verify(ev_tcpclient->ssldata.ssl_context, SSL_VERIFY_PEER, NULL);
	}

	if (!ev_tcpclient->ssldata.ssl_handle)
		ev_tcpclient->ssldata.ssl_handle	= SSL_new(ev_tcpclient->ssldata.ssl_context);

	/* Failed creating SSL HANDLE */
	if (!ev_tcpclient->ssldata.ssl_handle)
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Failed creating SSL_HANDLE\n", ev_tcpclient->socket_fd);
		return COMM_CLIENT_FAILURE_SSL_HANDLE;
	}

	/* Load TLS/SNI extension */
	if (ev_tcpclient->cfg.sni_hostname[0] != '\0')
	{
		/* Save a copy of HOSTNAME for TLS Server Name Indication */
		SSL_set_tlsext_host_name(ev_tcpclient->ssldata.ssl_handle, (char*)&ev_tcpclient->cfg.sni_hostname);
	}

	/* Attach to SOCKET_FD */
	SSL_set_fd(ev_tcpclient->ssldata.ssl_handle, ev_tcpclient->socket_fd);
	ev_tcpclient->ssldata.ssl_negotiatie_trycount	= 0;
	ev_tcpclient->flags.ssl_enabled					= 1;

	return COMM_CLIENT_INIT_OK;
}
/**************************************************************************************************************************/
int CommEvTCPClientSSLDataClean(CommEvTCPClient *ev_tcpclient)
{
	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - PROTO [%d] - Cleaning SSL_CONTEXT\n",
			ev_tcpclient->socket_fd, ev_tcpclient->cli_proto);

	/* If has info clean it */
	if (ev_tcpclient->ssldata.ssl_handle)
	{
		SSL_shutdown (ev_tcpclient->ssldata.ssl_handle);
		SSL_free (ev_tcpclient->ssldata.ssl_handle);
		ev_tcpclient->ssldata.ssl_handle = NULL;
	}

	if (ev_tcpclient->ssldata.ssl_context)
	{
		SSL_CTX_free (ev_tcpclient->ssldata.ssl_context);
		ev_tcpclient->ssldata.ssl_context = NULL;
	}

	if (ev_tcpclient->ssldata.x509_cert)
	{
		X509_free(ev_tcpclient->ssldata.x509_cert);
		ev_tcpclient->ssldata.x509_cert = NULL;
	}

	return COMM_CLIENT_INIT_OK;
}
/**************************************************************************************************************************/
int CommEvTCPClientSSLDataReset(CommEvTCPClient *ev_tcpclient)
{
	int op_status = 0;

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - PROTO [%d] - Resetting SSL_CONTEXT\n",
			ev_tcpclient->socket_fd, ev_tcpclient->cli_proto);

	/* Sanity check */
	if (COMM_CLIENTPROTO_SSL != ev_tcpclient->cli_proto)
		return COMM_CLIENT_INIT_OK;

	/* Clean up context */
	op_status 		= CommEvTCPClientSSLDataClean(ev_tcpclient);

	/* Failed cleaning context */
	if (COMM_CLIENT_INIT_OK != op_status)
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - PROTO [%d] - Failed resetting SSL_CONTEXT\n",
				ev_tcpclient->socket_fd, ev_tcpclient->cli_proto);

		return op_status;
	}

	op_status 		= CommEvTCPClientSSLDataInit(ev_tcpclient);

	return op_status;
}
/**************************************************************************************************************************/
int CommEvTCPClientSSLPeerVerify(CommEvTCPClient *ev_tcpclient)
{
    X509 *cert;
    char *line_ptr;
    int op_status;

	/* Sanity check */
	if (COMM_CLIENTPROTO_SSL != ev_tcpclient->cli_proto)
		return COMM_CLIENT_INIT_OK;

    /* Get server certificate */
    cert 			= SSL_get_peer_certificate(ev_tcpclient->ssldata.ssl_handle);

    if (cert == NULL)
    {
    	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Failed to get server certificate", ev_tcpclient->socket_fd);
    	return COMM_CLIENT_FAILURE_SSL_PEER;
    }

    /* Print certificate info */
    line_ptr 		= X509_NAME_oneline(X509_get_subject_name(cert), 0, 0);
    KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Server certificate subject: %s\n", ev_tcpclient->socket_fd, line_ptr);
    free(line_ptr);

    line_ptr 		= X509_NAME_oneline(X509_get_issuer_name(cert), 0, 0);
    KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Server certificate issuer: %s\n", ev_tcpclient->socket_fd, line_ptr);
    free(line_ptr);

    /* Do certificate validation */
    op_status 	= SSL_get_verify_result(ev_tcpclient->ssldata.ssl_handle);

    /* Check validation */
    if (X509_V_OK != op_status)
    {
    	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Certificate verification failed\n", ev_tcpclient->socket_fd, op_status);

        return COMM_CLIENT_FAILURE_SSL_PEER;
    }

    /* Release memory */
    X509_free(cert);

    return COMM_CLIENT_INIT_OK;
}
/**************************************************************************************************************************/
int CommEvTCPClientEventSSLNegotiate(int fd, int data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base				= base_ptr;
	CommEvTCPClient *ev_tcpclient	= cb_data;
	EvBaseKQFileDesc *kq_fd			= EvKQBaseFDGrabFromArena(ev_base, fd);
	int op_status;

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - SSL_HANDSHAKE try [%d]\n",
			ev_tcpclient->socket_fd, ev_tcpclient->ssldata.ssl_negotiatie_trycount);

	/* Increment count */
	ev_tcpclient->ssldata.ssl_negotiatie_trycount++;

	/* Too many negotiation retries, give up */
	if (ev_tcpclient->ssldata.ssl_negotiatie_trycount > 50)
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "FD [%d] - SSL_HANDSHAKE try [%d] - Too many retries, failing..\n",
				ev_tcpclient->socket_fd, ev_tcpclient->ssldata.ssl_negotiatie_trycount);
		goto negotiation_failed;
	}

	/* Clear libSSL errors and invoke SSL handshake mechanism */
	ERR_clear_error();
	op_status = SSL_connect(ev_tcpclient->ssldata.ssl_handle);

	/* Failed to connect on this try, check what is going on */
	if (op_status <= 0)
	{
		int ssl_error = SSL_get_error(ev_tcpclient->ssldata.ssl_handle, op_status);
		char err_buf[256];
		char *reason_str = NULL;

		/* NULL term uninit buffer */
		err_buf[sizeof(err_buf)  -1] = '\0';

		switch (ssl_error)
		{

		case SSL_ERROR_WANT_READ:
			EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventSSLNegotiate, ev_tcpclient);
			return 0;

		case SSL_ERROR_WANT_WRITE:
			EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventSSLNegotiate, ev_tcpclient);
			return 0;

		default:
		{
//			ERR_error_string_n(ssl_error, (char*)&err_buf, sizeof(err_buf));
//			reason_str = ERR_reason_error_string(ssl_error);
//
//			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "FD [%d] - SSL handshake failed with SSL_ERROR [%u] - [%s / %s]\n",
//					ev_tcpclient->socket_fd, ssl_error, err_buf, (reason_str ? reason_str : "NULL") );

			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - SSL handshake [%s] - failed with SSL_ERROR [%u]\n",
					ev_tcpclient->socket_fd, ev_tcpclient->cfg.hostname, ssl_error);

			goto negotiation_failed;
		}
		}
	}
	/* SSL connected OK */
	else
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - SSL handshake OK\n", ev_tcpclient->socket_fd);

		/* Set state as CONNECTED and grab PEER certificate information */
		ev_tcpclient->socket_state		= COMM_CLIENT_STATE_CONNECTED;
		ev_tcpclient->ssldata.x509_cert = SSL_get_peer_certificate(ev_tcpclient->ssldata.ssl_handle);

		/* XXX TODO: Check if SUBJECT_NAME or ALTERNATIVE_NAMEs match SNI host we connect to - Only protection to MITM */

		/* Schedule DATARATE CALCULATE TIMER timer */
		CommEvTCPClientRatesCalculateSchedule(ev_tcpclient, -1);

		/* Dispatch the internal event */
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Dispatch CONNECT\n", ev_tcpclient->socket_fd);
		CommEvTCPClientEventDispatchInternal(ev_tcpclient, data_sz, thrd_id, COMM_CLIENT_EVENT_CONNECT);

		/* Closed flag set, we are already destroyed, just bail out */
		if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
			return 0;

		ev_tcpclient->ssldata.ssl_negotiatie_trycount = 0;

		EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventSSLRead, ev_tcpclient);
		EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventEof, ev_tcpclient);

		return 1;
	}

	/* Negotiation failed label */
	negotiation_failed:

	/* Mark fail state and close socket */
	ev_tcpclient->socket_state = COMM_CLIENT_STATE_CONNECT_FAILED_NEGOTIATING_SSL;

	/* Dispatch the internal event */
	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d / %d] - FLAGS [%d / %d] - SSL_NEG_FAIL dispatch CONNECT [%s]\n",
			ev_tcpclient->socket_fd, kq_fd->fd.num, ev_tcpclient->flags.destroy_after_connect_fail, ev_tcpclient->flags.reconnect_on_fail, ev_tcpclient->cfg.hostname);

	CommEvTCPClientEventDispatchInternal(ev_tcpclient, data_sz, thrd_id, COMM_CLIENT_EVENT_CONNECT);

	/* Closed flag set, we are already destroyed, just bail out */
	if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "FD [%d] - SSL_NEG_FAIL on closed FD [%d /%d]\n",
				kq_fd->fd.num, kq_fd->flags.closed, kq_fd->flags.closing);
		return 0;
	}

	/* Internal CLEANUP */
	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - CALL DISCONNECT\n", ev_tcpclient->socket_fd);
	CommEvTCPClientInternalDisconnect(ev_tcpclient);

	/* Resetting SSL context */
	CommEvTCPClientSSLDataReset(ev_tcpclient);

	/* Upper layers want a full DESTROY if CONNECTION FAILS */
	if (ev_tcpclient->flags.destroy_after_connect_fail)
	{
		CommEvTCPClientDestroy(ev_tcpclient);
	}
	/* Upper layers want a reconnect retry if CONNECTION FAILS */
	else if (ev_tcpclient->flags.reconnect_on_fail)
	{
		/* Will close socket and cancel any pending events of ev_tcpclient->socket_fd, including the close event */
		if (ev_tcpclient->socket_fd >= 0)
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - CALL CLOSE\n", ev_tcpclient->socket_fd);

			EvKQBaseSocketClose(ev_tcpclient->kq_base, ev_tcpclient->socket_fd);
			ev_tcpclient->socket_fd = -1;
		}

		/* Destroy read and write buffers */
		CommEvTCPClientDestroyConnReadAndWriteBuffers(ev_tcpclient);

//		ev_tcpclient->socket_state	= COMM_CLIENT_STATE_DISCONNECTED;
		ev_tcpclient->socket_fd		= -1;

		/* Schedule RECONNECT timer */
		CommEvTCPClientReconnectSchedule(ev_tcpclient, -1);

		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Schedule RECONNECT_FAIL at TIMER_ID [%d]\n",
				ev_tcpclient->socket_fd, ev_tcpclient->timers.reconnect_id);
	}

	return 0;
}
/**************************************************************************************************************************/
int CommEvTCPClientSSLShutdown(int fd, int can_data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	char junk_buf[1024];
	int shutdown_state;
	int ssl_error;

	EvKQBase *ev_base				= base_ptr;
	CommEvTCPClient *ev_tcpclient	= cb_data;
	EvBaseKQFileDesc *kq_fd			= EvKQBaseFDGrabFromArena(ev_base, fd);

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Trying to shut down\n", ev_tcpclient->socket_fd);

	/* EOF set from PEER_SIDE, bail out */
	if ((kq_fd) && (kq_fd->flags.so_read_eof || kq_fd->flags.so_write_eof))
	{
		ev_tcpclient->flags.socket_eof = 1;
		goto do_shutdown;
	}

	/* Too many shutdown retries, bail out */
	if (ev_tcpclient->ssldata.ssl_shutdown_trycount++ > 10)
		goto do_shutdown;

	/* Not yet initialized, bail out */
	if (!SSL_is_init_finished(ev_tcpclient->ssldata.ssl_handle))
		goto do_shutdown;

	/* Clear SSL error queue and invoke shutdown */
	ERR_clear_error();
	shutdown_state = SSL_shutdown(ev_tcpclient->ssldata.ssl_handle);

	/* Check shutdown STATE - https://www.openssl.org/docs/ssl/SSL_shutdown.html */
	switch(shutdown_state)
	{
	/* The shutdown was not successful because a fatal error occurred either at the protocol level or a connection failure occurred. It can also occur if action is need to continue
	 * the operation for non-blocking BIOs. Call SSL_get_error(3) with the return value ret to find out the reason. */
	case -1:
	{
		goto check_error;
	}

	/* The shutdown is not yet finished. Call SSL_shutdown() for a second time, if a bidirectional shutdown shall be performed. The output of SSL_get_error(3) may
	 * be misleading, as an erroneous SSL_ERROR_SYSCALL may be flagged even though no error occurred. */
	case 0:
	{
		EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientSSLShutdown, ev_tcpclient);
		return 0;
	}
	/*	The shutdown was successfully completed. The ``close notify'' alert was sent and the peer's ``close notify'' alert was received. */
	case 1:
	default:
		goto do_shutdown;

	}

	/* TAG to examine SSL error and decide what to do */
	check_error:

	/* Grab error from SSL */
	ssl_error = SSL_get_error(ev_tcpclient->ssldata.ssl_handle, shutdown_state);

	switch (ssl_error)
	{
	case SSL_ERROR_WANT_READ:
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - SSL_ERROR_WANT_READ\n", ev_tcpclient->socket_fd);
		EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientSSLShutdown, ev_tcpclient);
		return 0;
	}
	case SSL_ERROR_WANT_WRITE:
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - SSL_ERROR_WANT_WRITE\n", ev_tcpclient->socket_fd);
		EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientSSLShutdown, ev_tcpclient);
		return 0;
	}
	case SSL_ERROR_ZERO_RETURN:
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - SSL_ERROR_ZERO_RETURN\n", ev_tcpclient->socket_fd);
		EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientSSLShutdown, ev_tcpclient);
		return 0;
	}

	case SSL_ERROR_SYSCALL:
	case SSL_ERROR_SSL:
	default:
		goto do_shutdown;
	}

	return 0;

	do_shutdown:

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Shutting down\n", ev_tcpclient->socket_fd);

	/* Dispatch event before destroying IO structures to give a chance for the operator to use it */
	if (((ev_tcpclient->flags.socket_eof) || (ev_tcpclient->flags.ssl_fatal_error)) && (!ev_tcpclient->flags.close_request))
		CommEvTCPClientEventDispatchInternal(ev_tcpclient, 0, thrd_id, COMM_CLIENT_EVENT_CLOSE);

	/*  Reset shutdown retry count and flags */
	ev_tcpclient->ssldata.ssl_shutdown_trycount = 0;
	ev_tcpclient->flags.ssl_shuting_down 		= 0;

	/* Either destroy or disconnect client, as requested by operator flags */
	COMM_EV_TCP_CLIENT_INTERNAL_FINISH(ev_tcpclient);

	return 1;
}
/**************************************************************************************************************************/
int CommEvTCPClientEventSSLRead(int fd, int can_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base				= base_ptr;
	CommEvTCPClient *ev_tcpclient	= cb_data;
	EvBaseKQFileDesc *kq_fd			= EvKQBaseFDGrabFromArena(ev_base, ev_tcpclient->socket_fd);

	char read_buf[COMM_TCP_SSL_READ_BUFFER_SZ];
	int ssl_bytes_read;
	int data_read;
	int ssl_error;

	/* Nothing to read, bail out */
	if (0 == can_read_sz)
		return 0;

	/* Clear libSSL errors and read from SSL tunnel */
	ERR_clear_error();
	ssl_bytes_read = SSL_read(ev_tcpclient->ssldata.ssl_handle, &read_buf, sizeof(read_buf) - 1);

	/* Touch RAW-SIDE statistics */
	ev_tcpclient->statistics.total[COMM_CURRENT].packet_rx	+= 1;

	/* Check errors */
	if (ssl_bytes_read <= 0)
	{
		ssl_error = SSL_get_error(ev_tcpclient->ssldata.ssl_handle, ssl_bytes_read);

//			SSL_ERROR_NONE                  0
//			SSL_ERROR_SSL                   1
//			SSL_ERROR_WANT_READ             2
//			SSL_ERROR_WANT_WRITE            3
//			SSL_ERROR_WANT_X509_LOOKUP      4
//			SSL_ERROR_SYSCALL               5/* look at error stack/return value/errno */
//			SSL_ERROR_ZERO_RETURN           6
//			SSL_ERROR_WANT_CONNECT          7
//			SSL_ERROR_WANT_ACCEPT           8
//			SSL_ERROR_WANT_ASYNC            9
//			SSL_ERROR_WANT_ASYNC_JOB       10

		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - SSL_ERROR [%d]\n", ev_tcpclient->socket_fd, ssl_error);

		switch (ssl_error)
		{
		case SSL_ERROR_WANT_READ:
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - SSL_ERROR_WANT_READ [%d]\n", ev_tcpclient->socket_fd, SSL_ERROR_WANT_READ);
			EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventSSLRead, ev_tcpclient);
			return 0;
		}
		case SSL_ERROR_WANT_WRITE:
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - SSL_ERROR_WANT_WRITE [%d]\n", ev_tcpclient->socket_fd, SSL_ERROR_WANT_WRITE);
			EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventSSLRead, ev_tcpclient);
			return 0;
		}
		/* Receive peer close notification, move to shutdown */
		case SSL_ERROR_NONE:
		case SSL_ERROR_ZERO_RETURN:
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - SSL_ERROR_NONE [%d] - SSL_ERROR_ZERO_RETURN [%d]\n", ev_tcpclient->socket_fd, SSL_ERROR_NONE, SSL_ERROR_ZERO_RETURN);
			ev_tcpclient->flags.ssl_zero_ret = 1;
			EvKQBaseSetEvent(ev_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventSSLRead, ev_tcpclient);
			return 0;
		}

		case SSL_ERROR_SYSCALL:
		default:
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - SSL_ERROR_SYSCALL [%d] - DEFAULT\n", ev_tcpclient->socket_fd, SSL_ERROR_SYSCALL);
			/* Mark flags as fatal error */
			ev_tcpclient->flags.ssl_fatal_error = 1;

			/* Trigger the SSL shutdown mechanism (post-2008) */
			CommEvTCPClientSSLShutdownBegin(ev_tcpclient);
			return can_read_sz;
		}

		}
	}
	/* Read into operator defined structure */
	else
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - SUCCESS READING [%d] bytes - SZ [%d]\n",
				ev_tcpclient->socket_fd, ssl_bytes_read, can_read_sz);

		data_read = CommEvTCPClientProcessBuffer(ev_tcpclient, can_read_sz, thrd_id,  (char *)&read_buf, ssl_bytes_read);

		/* We are CLOSED, bail out */
		if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
			return data_read;

//		/* NON fatal error while reading this FD, reschedule and leave */
//		if (-1 == data_read)
//		{
//			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Non fatal error while trying to read [%d] bytes\n", fd, data_read);
//		}

		/* Touch SSL-SIDE and RAW-SIDE statistics */
		if (data_read > 0)
		{
			ev_tcpclient->statistics.total[COMM_CURRENT].ssl_byte_rx	+= ssl_bytes_read;
			ev_tcpclient->statistics.total[COMM_CURRENT].byte_rx		+= ssl_bytes_read;
		}
	}

	/* Reschedule read event - Upper layers could have closed this socket, so just RESCHEDULE READ if we are still ONLINE */
	if ( ((!kq_fd->flags.closed) && (!kq_fd->flags.closing)) && (ev_tcpclient->socket_state == COMM_CLIENT_STATE_CONNECTED))
		EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventSSLRead, ev_tcpclient);

	/* SSL read must return zero so lower layers do not get tricked by missing bytes in kernel */
	return 0;
}
/**************************************************************************************************************************/
int CommEvTCPClientEventSSLWrite(int fd, int can_write_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvAIOReq *cur_aio_req;
	int ssl_bytes_written;
	int ssl_error;

	EvKQBase *ev_base				= base_ptr;
	CommEvTCPClient *ev_tcpclient	= cb_data;
	int total_ssl_bytes_written		= 0;
	EvBaseKQFileDesc *kq_fd			= EvKQBaseFDGrabFromArena(ev_base, ev_tcpclient->socket_fd);

	/* Client is going down, bail out */
	if (can_write_sz <= 0)
		return 0;

	assert(ev_tcpclient->ssldata.ssl_handle);

	/* Grab aio_req unit */
	cur_aio_req	= EvAIOReqQueueDequeue(&ev_tcpclient->iodata.write_queue);

	/* Nothing to write, bail out */
	if (!cur_aio_req)
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Empty WRITE_LIST.. STOP\n",	fd);

		/* Reset pending write flag */
		ev_tcpclient->flags.pending_write = 0;
		return total_ssl_bytes_written;
	}

	/* Touch statistics */
	ev_tcpclient->statistics.total[COMM_CURRENT].packet_tx	+= 1;

	/* Clear libSSL errors and write it to SSL tunnel */
	ERR_clear_error();
	ssl_bytes_written = SSL_write(ev_tcpclient->ssldata.ssl_handle, cur_aio_req->data.ptr, cur_aio_req->data.size);

	/* Failed writing to SSL tunnel */
	if (ssl_bytes_written <= 0)
	{
		/* Grab SSL error to process */
		ssl_error = SSL_get_error(ev_tcpclient->ssldata.ssl_handle, ssl_bytes_written);

		/* Push AIO_REQ queue back for writing and set pending write flag */
		EvAIOReqQueueEnqueueHead(&ev_tcpclient->iodata.write_queue, cur_aio_req);
		ev_tcpclient->flags.pending_write = 1;

		/* Decide based on SSL error */
		switch (ssl_error)
		{
		case SSL_ERROR_WANT_READ:
		{

			EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventSSLWrite, ev_tcpclient);
			return 0;
		}
		case SSL_ERROR_WANT_WRITE:
		{
			EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventSSLWrite, ev_tcpclient);
			return 0;
		}

		/* Receive peer close notification, move to shutdown */
		case SSL_ERROR_NONE:
		case SSL_ERROR_ZERO_RETURN:
		{
			EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventSSLWrite, ev_tcpclient);
			return 0;
		}

		case SSL_ERROR_SYSCALL:
		default:
		{
			/* Mark flags as fatal error and pending write */
			ev_tcpclient->flags.ssl_fatal_error = 1;

			/* Trigger the SSL shutdown mechanism (post-2008) */
			CommEvTCPClientSSLShutdownBegin(ev_tcpclient);
			return 0;
		}
		}

		return 0;
	}

	/* Touch RAW and SSL statistics */
	ev_tcpclient->statistics.total[COMM_CURRENT].byte_tx		+= cur_aio_req->data.size;
	ev_tcpclient->statistics.total[COMM_CURRENT].ssl_byte_tx	+= ssl_bytes_written;
	total_ssl_bytes_written										+= ssl_bytes_written;

	/* Invoke notification CALLBACKS if not CLOSE_REQUEST and then destroy AIO REQ */
	if (!ev_tcpclient->flags.close_request)
		EvAIOReqInvokeCallBacks(cur_aio_req, 1, fd, ssl_bytes_written, thrd_id, ev_tcpclient);
	EvAIOReqDestroy(cur_aio_req);

	/* Closed flag set, we are already destroyed, just bail out */
	if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
		return can_write_sz;

	/* Reschedule write event */
	if (!EvAIOReqQueueIsEmpty(&ev_tcpclient->iodata.write_queue))
	{
		/* SET pending write flag */
		ev_tcpclient->flags.pending_write = 1;
		EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventSSLWrite, ev_tcpclient);

		return can_write_sz;
	}
	/* Write list is empty */
	else
	{
		/* Reset pending write flag */
		ev_tcpclient->flags.pending_write = 0;

		/* Upper layers requested to close after writing all */
		if (ev_tcpclient->flags.close_request)
		{
			KQBASE_LOG_PRINTF(ev_base->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Upper layer set CLOSE_REQUEST, write buffer is empty - [%s]\n",
					ev_tcpclient->socket_fd, ev_tcpclient->flags.destroy_after_close ? "DESTROYING" : "CLOSING");

			/* Trigger the SSL shutdown mechanism (post-2008) */
			CommEvTCPClientSSLShutdownBegin(ev_tcpclient);
			return can_write_sz;
		}

		return can_write_sz;
	}

	return can_write_sz;
}
/**************************************************************************************************************************/
/**/
/**************************************************************************************************************************/
static int CommEvTCPClientSSLShutdownJob(void *job, void *cbdata_ptr)
{
	CommEvTCPClient *ev_tcpclient	= cbdata_ptr;

	/* Reset shutdown JOB ID */
	ev_tcpclient->ssldata.shutdown_jobid = -1;

	/* Clean any pending TIMEOUT */
	EvKQBaseTimeoutClearAll(ev_tcpclient->kq_base, ev_tcpclient->socket_fd);

	/* Now remove any READ/WRITE events, then remove DEFER and only then restore READ/WRITE events. This will avoid wandering events from being dispatched to SSL_SHUTDOWN */
	EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_DELETE, NULL, NULL);
	EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_DELETE, NULL, NULL);

	/* Cancel LOWER LEVEL DEFER_READ and DEFER_WRITE events */
	EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_DEFER_CHECK_READ, COMM_ACTION_DELETE, NULL, NULL);
	EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_DEFER_CHECK_WRITE, COMM_ACTION_DELETE, NULL, NULL);

	/* Redirect READ/WRITE events to SSL_SHUTDOWN */
	EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientSSLShutdown, ev_tcpclient);
	EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientSSLShutdown, ev_tcpclient);

	/* Fire up SSL_shutdown sequence */
	ev_tcpclient->ssldata.ssl_shutdown_trycount = 0;
	CommEvTCPClientSSLShutdown(ev_tcpclient->socket_fd, 0, -1, ev_tcpclient, ev_tcpclient->kq_base);

	return 1;
}
/**************************************************************************************************************************/
