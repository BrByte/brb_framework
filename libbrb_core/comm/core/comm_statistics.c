/*
 * comm_statistics.c
 *
 *  Created on: 2014-06-20
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

#include "../include/libbrb_core.h"

/**************************************************************************************************************************/
void CommEvStatisticsRateCalculate(EvKQBase *ev_base, CommEvStatistics *statistics, int socket_fd, int rates_type)
{
	struct timeval *current_tv;
	struct timeval *previous_tv;

	long delta_bytes_ssl;
	long delta_bytes;
	long delta_packets;

	long delta_user00;
	long delta_user01;
	long delta_user02;
	long delta_user03;

	int delta_time_ms;

	statistics->ev_base = ev_base;

	/* Sanity check */
	if (rates_type >= COMM_RATES_LASTITEM)
		return;

	switch(rates_type)
	{

	/*****************************************************************************/
	case COMM_RATES_READ:
	{
		previous_tv		= &statistics->last_read_tv;
		current_tv		= &ev_base->stats.cur_invoke_tv;

		/* First iteration, update previous and leave */
		if (0 == previous_tv->tv_sec)
		{
			statistics->last_read_ts = ev_base->stats.cur_invoke_ts_sec;
			memcpy(previous_tv, current_tv, sizeof(struct timeval));
			break;
		}

		delta_time_ms	= EvKQBaseTimeValSubMsec(previous_tv, current_tv);

		if (delta_time_ms <= 0)
			break;

		delta_bytes_ssl	= statistics->total[COMM_CURRENT].ssl_byte_rx - statistics->total[COMM_PREVIOUS].ssl_byte_rx;
		delta_bytes		= statistics->total[COMM_CURRENT].byte_rx - statistics->total[COMM_PREVIOUS].byte_rx;
		delta_packets	= statistics->total[COMM_CURRENT].packet_rx - statistics->total[COMM_PREVIOUS].packet_rx;

		statistics->rate.ssl_byte_rx	= (((float)(delta_bytes_ssl / delta_time_ms) * 1000.00) * 8.00);
		statistics->rate.byte_rx		= (((float)(delta_bytes / delta_time_ms) * 1000.00) * 8.00);
		statistics->rate.packet_rx		= ((float)(delta_packets / delta_time_ms) * 1000.00);

		/* Update previous from current */
		statistics->total[COMM_PREVIOUS].ssl_byte_rx	= statistics->total[COMM_CURRENT].ssl_byte_rx;
		statistics->total[COMM_PREVIOUS].byte_rx		= statistics->total[COMM_CURRENT].byte_rx;
		statistics->total[COMM_PREVIOUS].packet_rx		= statistics->total[COMM_CURRENT].packet_rx;

		/* Update time stamp */
		statistics->last_read_ts = ev_base->stats.cur_invoke_ts_sec;
		memcpy(previous_tv, current_tv, sizeof(struct timeval));

		//KQBASE_LOG_PRINTF(ev_base->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW,
		//		"FD [%d] - COMM_RATES_READ - DELTAS [TIME_MS (%d) - BYTES_SSL (%d) - BYTES (%d) - PACKETS (%d)] - RATE_PACKET [%.2f] RATE_KBPS PLAIN/SSL [%.2f / %.2f]\n",
		//		socket_fd, delta_time_ms, delta_bytes_ssl, delta_bytes, delta_packets,
		//		COMM_EV_STATS_PTR_READ_PACKET_RX(statistics), (COMM_EV_STATS_PTR_READ_BYTE_RX(statistics) / 1024), (COMM_EV_STATS_PTR_READ_SSL_BYTE_RX(statistics) / 1024));
		break;

	}
	/*****************************************************************************/
	case COMM_RATES_WRITE:
	{
		previous_tv	= &statistics->last_write_tv;
		current_tv	= &ev_base->stats.cur_invoke_tv;

		/* First iteration, update previous and leave */
		if (0 == previous_tv->tv_sec)
		{
			statistics->last_write_ts = ev_base->stats.cur_invoke_ts_sec;
			memcpy(previous_tv, current_tv, sizeof(struct timeval));
			break;
		}

		delta_time_ms	= EvKQBaseTimeValSubMsec(previous_tv, current_tv);

		if (delta_time_ms <= 0)
			break;

		delta_bytes_ssl	= statistics->total[COMM_CURRENT].ssl_byte_tx - statistics->total[COMM_PREVIOUS].ssl_byte_tx;
		delta_bytes		= statistics->total[COMM_CURRENT].byte_tx - statistics->total[COMM_PREVIOUS].byte_tx;
		delta_packets	= statistics->total[COMM_CURRENT].packet_tx - statistics->total[COMM_PREVIOUS].packet_tx;

		statistics->rate.ssl_byte_tx	= (((float)(delta_bytes_ssl / delta_time_ms) * 1000.00) * 8.00);
		statistics->rate.byte_tx		= (((float)(delta_bytes / delta_time_ms) * 1000.00) * 8.00);
		statistics->rate.packet_tx		= ((float)(delta_packets / delta_time_ms) * 1000.00);

		/* Update previous from current */
		statistics->total[COMM_PREVIOUS].ssl_byte_tx	= statistics->total[COMM_CURRENT].ssl_byte_tx;
		statistics->total[COMM_PREVIOUS].byte_tx		= statistics->total[COMM_CURRENT].byte_tx;
		statistics->total[COMM_PREVIOUS].packet_tx		= statistics->total[COMM_CURRENT].packet_tx;

		/* Update time stamp */
		statistics->last_write_ts = ev_base->stats.cur_invoke_ts_sec;
		memcpy(previous_tv, current_tv, sizeof(struct timeval));

		//KQBASE_LOG_PRINTF(ev_base->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW,
		//		"FD [%d] - COMM_RATES_WRITE - DELTAS [TIME_MS (%d) - BYTES_PLAIN_SSL (%d / %d) - PACKETS (%d)] - RATE_PACKET [%.2f] RATE_KBPS PLAIN/SSL [%.2f / %.2f]\n",
		//		socket_fd, delta_time_ms, delta_bytes_ssl, delta_bytes, delta_packets,
		//		COMM_EV_STATS_PTR_READ_PACKET_TX(statistics), (COMM_EV_STATS_PTR_READ_BYTE_TX(statistics) / 1024), (COMM_EV_STATS_PTR_READ_SSL_BYTE_TX(statistics) / 1024));

		break;
	}
	/*****************************************************************************/
	case COMM_RATES_USER:
	{
		previous_tv	= &statistics->last_user_tv;
		current_tv	= &ev_base->stats.cur_invoke_tv;

		/* First iteration, update previous and leave */
		if (0 == previous_tv->tv_sec)
		{
			statistics->last_user_ts = ev_base->stats.cur_invoke_ts_sec;
			memcpy(previous_tv, current_tv, sizeof(struct timeval));
			break;
		}

		delta_time_ms	= EvKQBaseTimeValSubMsec(previous_tv, current_tv);

		if (delta_time_ms <= 0)
			break;

		delta_user00 = statistics->total[COMM_CURRENT].user00 - statistics->total[COMM_PREVIOUS].user00;
		delta_user01 = statistics->total[COMM_CURRENT].user01 - statistics->total[COMM_PREVIOUS].user01;
		delta_user02 = statistics->total[COMM_CURRENT].user02 - statistics->total[COMM_PREVIOUS].user02;
		delta_user03 = statistics->total[COMM_CURRENT].user03 - statistics->total[COMM_PREVIOUS].user03;

		statistics->rate.user00	= (((float)(delta_user00 / delta_time_ms) * 1000.00) * 8.00);
		statistics->rate.user01	= (((float)(delta_user01 / delta_time_ms) * 1000.00) * 8.00);
		statistics->rate.user02	= (((float)(delta_user02 / delta_time_ms) * 1000.00) * 8.00);
		statistics->rate.user03	= (((float)(delta_user03 / delta_time_ms) * 1000.00) * 8.00);

		/* Update previous from current */
		statistics->total[COMM_PREVIOUS].user00	= statistics->total[COMM_CURRENT].user00;
		statistics->total[COMM_PREVIOUS].user01	= statistics->total[COMM_CURRENT].user01;
		statistics->total[COMM_PREVIOUS].user02	= statistics->total[COMM_CURRENT].user02;
		statistics->total[COMM_PREVIOUS].user03	= statistics->total[COMM_CURRENT].user03;

		/* Update time stamp */
		statistics->last_user_ts = ev_base->stats.cur_invoke_ts_sec;
		memcpy(previous_tv, current_tv, sizeof(struct timeval));

		//KQBASE_LOG_PRINTF(ev_base->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW,
		//		"FD [%d] - COMM_RATES_USER - DELTAS [TIME_MS (%d) - USER_TOTAL (%ld / %d /%ld /%ld)] - USER_RATE [%.2f / %.2f / %.2f / %.2f]\n",
		//		socket_fd, delta_time_ms, delta_user00, delta_user01, delta_user02, delta_user03,
		//		COMM_EV_STATS_PTR_READ_USER00(statistics), COMM_EV_STATS_PTR_READ_USER01(statistics),
		//		COMM_EV_STATS_PTR_READ_USER02(statistics), COMM_EV_STATS_PTR_READ_USER03(statistics));

		break;
	}
	/*****************************************************************************/
	}

	return;
}
/**************************************************************************************************************************/
char *CommEvStatisticsBytesHumanize(long total_bytes, char *buf_ptr, int buf_maxsz)
{
	float total_f;
	int offset;
	int i;

	char *type_str	= "B";
	buf_ptr[0]		= '\0';

	/* Recursively divide RATE by 1024 */
	for (total_f = total_bytes, i = 0; (total_f > 1024); i++)
		total_f = (total_f / 1024);

	/* Decide legend string */
	switch(i)
	{
	case 0: type_str = "B";	break;
	case 1: type_str = "KB";	break;
	case 2: type_str = "MB";	break;
	case 3: type_str = "GB";	break;
	case 4: type_str = "TB";	break;
	default: break;
	}

	/* Generate string representation of this DATA_RATE */
	offset = snprintf(buf_ptr, (buf_maxsz - 1), "%2.2f %s", total_f, type_str);
	buf_ptr[offset] = '\0';

	return buf_ptr;
}
/**************************************************************************************************************************/
char *CommEvStatisticsRateHumanize(long rate, char *buf_ptr, int buf_maxsz)
{
	float ratef;
	int offset;
	int i;

	char *type_str	= "bps";
	buf_ptr[0]		= '\0';

	/* Recursively divide RATE by 1024 */
	for (ratef = rate, i = 0; (ratef > 1024); i++)
		ratef = (ratef / 1024);

	/* Decide legend string */
	switch(i)
	{
	case 0: type_str = "bps";	break;
	case 1: type_str = "Kbps";	break;
	case 2: type_str = "Mbps";	break;
	case 3: type_str = "Gbps";	break;
	case 4: type_str = "Tbps";	break;
	default: break;
	}

	/* Generate string representation of this DATA_RATE */
	offset = snprintf(buf_ptr, (buf_maxsz - 1), "%2.2f %s", ratef, type_str);
	buf_ptr[offset] = '\0';

	return buf_ptr;
}
/**************************************************************************************************************************/
char *CommEvStatisticsRatePPSHumanize(long rate, char *buf_ptr, int buf_maxsz)
{
	float ratef;
	int offset;
	int i;

	char *type_str	= "pps";
	buf_ptr[0]		= '\0';

	/* Recursively divide RATE by 1024 */
	for (ratef = rate, i = 0; (ratef > 1024); i++)
		ratef = (ratef / 1024);

	/* Decide legend string */
	switch(i)
	{
	case 0: type_str = "pps";	break;
	case 1: type_str = "Kpps";	break;
	case 2: type_str = "Mpps";	break;
	case 3: type_str = "Gpps";	break;
	case 4: type_str = "Tpps";	break;
	default: break;
	}

	/* Generate string representation of this DATA_RATE */
	offset = snprintf(buf_ptr, (buf_maxsz - 1), "%2.2f %s", ratef, type_str);
	buf_ptr[offset] = '\0';

	return buf_ptr;
}
/**************************************************************************************************************************/
char *CommEvStatisticsPacketsHumanize(long total_packets, char *buf_ptr, int buf_maxsz)
{
	float total_f;
	int offset;
	int i;

	char *type_str	= "pkt";
	buf_ptr[0]		= '\0';

	/* Recursively divide RATE by 1024 */
	for (total_f = total_packets, i = 0; (total_f > 1024); i++)
		total_f = (total_f / 1024);

	/* Decide legend string */
	switch(i)
	{
	case 0: type_str = "pkt";	break;
	case 1: type_str = "Kpkt";	break;
	case 2: type_str = "Mpkt";	break;
	case 3: type_str = "Gpkt";	break;
	case 4: type_str = "Tpkt";	break;
	default: break;
	}

	/* Generate string representation of this DATA_RATE */
	offset = snprintf(buf_ptr, (buf_maxsz - 1), "%2.2f %s", total_f, type_str);
	buf_ptr[offset] = '\0';

	return buf_ptr;
}
/**************************************************************************************************************************/
char *CommEvStatisticsUptimeHumanize(long total_sec, char *buf_ptr, int buf_maxsz)
{
	int offset;

	/* Generate humanized UPTIME string */
	offset = snprintf(buf_ptr, (buf_maxsz - 1), "%02ldday %02ldhr %02ldmin %02ldsec",
			((total_sec / 3600) / 24), ((total_sec / 3600) % 24), ((total_sec % 3600) / 60), (total_sec % 60));
	buf_ptr[offset] = '\0';

	return buf_ptr;
}
/**************************************************************************************************************************/
void CommEvStatisticsClean(CommEvStatistics *statistics)
{
	/* Reset time stamps and values */
	memset(&statistics->last_read_tv, 0, sizeof(statistics->last_read_tv));
	memset(&statistics->last_write_tv, 0, sizeof(statistics->last_write_tv));
	memset(&statistics->last_user_tv, 0, sizeof(statistics->last_user_tv));

	statistics->last_read_ts	= 0;
	statistics->last_write_ts	= 0;
	statistics->last_user_ts	= 0;

	/* Clean up total and rates */
	memset(&statistics->total, 0, sizeof(statistics->total));
	memset(&statistics->rate, 0, sizeof(statistics->rate));

	return;
}
/**************************************************************************************************************************/
void CommEvStatisticsRateClean(CommEvStatistics *statistics)
{
	/* Clean up rates */
	memset(&statistics->rate, 0, sizeof(statistics->rate));
	return;
}
/**************************************************************************************************************************/
