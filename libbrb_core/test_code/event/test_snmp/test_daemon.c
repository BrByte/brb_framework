/*
 * test_daemon.c
 *
 *  Created on: 2014-09-13
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
#include "test_daemon.h"

EvKQBase *glob_ev_base;
EvKQBaseLogBase *glob_log_base;
int glob_timerid;

static EvBaseKQCBH mainTimerEvent;

static int keep_running;
static void signalStopServer(int signal);
static int init_server(void);

/**************************************************************************************************************************/
int main(int argc, char **argv)
{
	EvKQBaseLogBaseConf log_conf;
	int i;

	printf("RUN 01\n");

	memset(&log_conf, 0, sizeof(EvKQBaseLogBaseConf));
	log_conf.flags.double_write				= 1;
	log_conf.flags.mem_keep_logs			= 1;
//	log_conf.flags.mem_only_logs			= 1;
//	log_conf.mem_limit.bytes_total			= 8092;
//	log_conf.mem_limit.lines_total			= 20;

	/* Create event base */
	glob_ev_base	= EvKQBaseNew(NULL);
	glob_log_base	= EvKQBaseLogBaseNew(glob_ev_base, &log_conf);

	keep_running = 1;

	/* Intercept signals using default KQ_BASE signal handler */
	EvKQBaseInterceptSignals(glob_ev_base);
	signal(EV_SIGTERM, signalStopServer);
	signal(EV_SIGINT, signalStopServer);

	glob_ev_base->log_base = glob_log_base;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "TEST_DAEMON starting [%d]\n", 1);

	init_server();

	for (i = 0; i < 16; i++)
	{
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "LOG_LINE [%d]\n", i);
		continue;
	}

	glob_timerid 	= EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 1000, mainTimerEvent, NULL);

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "PID [%d] - Begin on TIMER_ID [%d]\n", getpid(), glob_timerid);

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, 100);
	exit(0);

	return 1;
}
/**************************************************************************************************************************/
static void signalStopServer(int signal)
{
    keep_running = 0;

	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int mainTimerEvent(int timer_id, int unused, int thrd_id, void *cb_data, void *base_ptr)
{
	/* Reschedule TIMER */
	glob_timerid 		= EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 1000, mainTimerEvent, NULL);

	printf("TIMER %d\n", glob_timerid);

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "PID [%d] - Reschedule on TIMER_ID [%d]\n", getpid(), glob_timerid);

	return 1;
}
/**************************************************************************************************************************/
static int init_server(void)
{
  int agentx_subagent=1; /* change this if you want to be a SNMP master agent */
//  int background = 0; /* change this if you want to run in the background */

  snmp_enable_stderrlog();

  /* we're an agentx subagent? */
  if (agentx_subagent)
  {
    /* make us a agentx client. */
    netsnmp_ds_set_boolean(NETSNMP_DS_APPLICATION_ID, NETSNMP_DS_AGENT_ROLE, 1);
  }

//  /* run in background, if requested */
//  if (background && netsnmp_daemonize(1, 1))
//      exit(1);

  /* initialize tcpip, if necessary */
  SOCK_STARTUP;

  /* initialize the agent library */
  init_agent("example-demon");

  /* initialize mib code here */

  /* mib code: init_nstAgentSubagentObject from nstAgentSubagentObject.C */
  init_nstAgentSubagentObject();

//  /* initialize vacm/usm access control  */
//  if (!agentx_subagent)
//  {
//      init_vacm_vars();
//      init_usmUser();
//  }

  /* example-demon will be used to read example-demon.conf files. */
  init_snmp("example-demon");

//  /* If we're going to be a snmp master agent, initial the ports */
//  if (!agentx_subagent)
//    init_master_agent();  /* open the port to listen on (defaults to udp:161) */

  snmp_log(LOG_INFO, "example-demon is up and running.\n");

  /* your main loop here... */
  while (keep_running)
  {
    /* if you use select(), see snmp_select_info() in snmp_api(3) */
    /*     --- OR ---  */
    agent_check_and_process(1); /* 0 == don't block */
  }

  /* at shutdown time */
  snmp_shutdown("example-demon");
  SOCK_CLEANUP;

  return 0;
}
/**************************************************************************************************************************/


