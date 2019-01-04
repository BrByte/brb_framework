/*
 * ev_kq_daemon.c
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

#include "../include/libbrb_ev_kq.h"
#include <sys/wait.h>

static int EvKQBaseDaemonTTYDettach(void);

/**************************************************************************************************************************/
void EvKQBaseDaemonForkAndReload(EvKQBase *kq_base, char *path_str, char *human_str)
{
	int wait_status;
	int child_pid;
	int pid;

	child_pid = fork();

	if (child_pid == 0)
	{
		pid = getpid();
		execl(path_str, (human_str ? human_str : path_str), NULL);
		exit(0);
	}

	pid = getpid();

	/* Wait for child to be OK, then exit */
	waitpid(child_pid, &wait_status, 0);
	exit(1);

	/* Never reached */
	return;
}
/**************************************************************************************************************************/
void EvKQBaseDaemonForkAndDetach(EvKQBase *kq_base)
{
	/* If GET_PARENT_PID returns different than 1, it mean we have been forked  */
	if (getppid() != 1)
	{
		/* Parent returns - Just exit parent */
		if (fork() != 0)
			exit(0);

		/* Detach child TTY */
		EvKQBaseDaemonTTYDettach();
		setsid();
	}

	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int EvKQBaseDaemonTTYDettach(void)
{
	int fd = -1;

	setpgid(0, getpid());
	fd = open(LIBBRB_TTYDEV, O_RDWR);

	/* Failed to open control TTY */
	if (fd < 0)
		return 0;

	/* Detach */
	ioctl(fd, TIOCNOTTY, 0);
	close(fd);

	return 1;
}
/**************************************************************************************************************************/


//void _DoBacktrace(char *dump_file, int signal)
//{
//
//	char backtrace_entry[256];
//	char signal_message[512];
//	char backtrace_file[512];
//	char backtrace_dump_str[8092];
//
//	void *backtrace_array[16];
//	char **backtrace_strings;
//
//	int backtrace_fd;
//	int backtrace_dump_sz;
//	int i;
//
//	size_t backtrace_size;
//
//	memset(&backtrace_entry, 0, 255);
//	memset(&signal_message, 0, 511);
//	memset(&backtrace_file, 0, 511);
//	memset(&backtrace_dump_str, 0, 8091);
//
//	/* Create actual backtrace file from received dump file format */
//	sprintf((char*)&backtrace_file, backtrace_path, getUnixTimeStamp() );
//
//	/* Try to open a file for backtrace dumping */
//	backtrace_fd = open(backtrace_file, O_WRONLY | O_TRUNC | O_CREAT);
//
//	/* Append initial backtrace message */
//	sprintf((char*)&signal_message, "Backtracing on signal [%d]...\n", signal );
//	strcat((char*)&backtrace_dump_str, (char*)&signal_message);
//
//	backtrace_dump_sz = strlen(backtrace_dump_str);
//
//	/* Write signal message */
//	write(backtrace_fd, backtrace_dump_str, backtrace_dump_sz);
//
//	/* Just backtrace frames in case of this signal */
//	if ((signal == 10) || (signal == 11))
//	{
//		/* Do backtrace */
//		backtrace_size = backtrace (backtrace_array, 16);
//		backtrace_strings = (char**)backtrace_symbols (backtrace_array, backtrace_size);
//
//		/* Humanize and dump to file */
//		if (backtrace_fd > 0)
//		{
//			for (i = 0; i < backtrace_size; i++)
//			{
//				sprintf((char*)&backtrace_entry, "BACKTRACE_ENTRY[%d] - %s\n", i, backtrace_strings[i]);
//				strcat((char*)&backtrace_dump_str, (char*)&backtrace_entry);
//			}
//			backtrace_dump_sz = strlen(backtrace_dump_str);
//
//			/* Write it to backtrace file */
//			write(backtrace_fd, backtrace_dump_str, backtrace_dump_sz);
//
//		}
//	}
//
//	close(backtrace_fd);
//	return;
//}
//
//
//void _BacktraceSignalHandler(int signal)
//{
//
//	/* Do backtrace */
//	_DoBacktrace(backtrace_path, signal);
//
//	exit(0);
//
//	return;
//}
//
//void
//_SignalHandler(int signal)
//{
//	register int i;
//	int child_pid, pid, wait_status;
//	char buf[64];
//
//	/* Ignore SIGPIPE */
//	if (signal == SIGPIPE)
//		return;
//
//	/* Do backtrace */
//	_DoBacktrace(backtrace_path, signal);
//
//	child_pid = fork();
//
//	if (child_pid == 0)
//	{
//		pid = getpid();
//		execl("/usr/local/sbin/apid", "apid", NULL);
//		exit(0);
//	}
//
//	pid = getpid();
//
//	/* Wait for child to be ok */
//	waitpid(child_pid, &wait_status, 0);
//
//	exit(1);
//}
//
//int
//_DetachTTY(void)
//{
//	int fd = -1;
//	setpgrp(0, getpid());
//
//	if ((fd = open(TTYDEV, O_RDWR)) >= 0)
//	{
//		ioctl(fd, TIOCNOTTY, 0); /* detach */
//		close(fd);
//		return 1;
//	}
//	else
//		return -1;
//}

