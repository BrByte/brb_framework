/*
 * ev_kq_logger.c
 *
 *  Created on: 2014-06-14
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

static EvKQBaseLogMemEntry *EvKQBaseLogBaseMemLogAdd(EvKQBaseLogBase *log_base, char *color_str, char *line_str, int line_sz);
static int EvKQBaseLogBaseMemLogDelete(EvKQBaseLogBase *log_base, EvKQBaseLogMemEntry *log_entry);
static int EvKQBaseLogBaseMemEnforceLimit(EvKQBaseLogBase *log_base);
static int EvKQBaseLogBaseDoWrite(EvKQBaseLogBase *log_base, char *color_str, char *line_str, int line_sz);
static int EvKQBaseLogBaseDoDestroy(EvKQBaseLogBase *log_base);
static void EvKQBaseLoggerAddDontLock(EvKQBaseLogBase *log_base, int type, int color, const char *file, const char *func, const int line, const char *message, ...);
static EvBaseKQCBH EvKQBaseLoggerTimerEvent;
static EvBaseKQCBH EvKQBaseLogBaseFileMonCB;

/**************************************************************************************************************************/
EvKQBaseLogBase *EvKQBaseLogBaseNew(EvKQBase *ev_base, EvKQBaseLogBaseConf *log_conf)
{
	EvKQBaseLogBase *log_base;

	/* Create and initialize LOG BASE */
	log_base						= calloc(1, sizeof(EvKQBaseLogBase));
	log_base->ev_base				= ev_base;
	log_base->mutex					= (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	log_base->lastmsg.timer_id		= -1;

	/* Load log section and level - If there is no log level set, default to INFO*/
	log_base->log_section			= log_conf->log_section;
	log_base->log_level				= ((log_conf->log_level > LOGTYPE_UNINIT) ? log_conf->log_level : LOGTYPE_INFO);

	/* Load flags from configuration structure */
	memcpy(&log_base->flags, &log_conf->flags, sizeof(log_base->flags));

	/* Load default limits */
	log_base->mem.bytes_total_limit = 16777216L;
	log_base->mem.lines_total_limit = 8092;

	/* Load CONFIG limits */
	if (log_conf->mem_limit.bytes_total > 0)
	{
		log_base->mem.bytes_total_limit = log_conf->mem_limit.bytes_total;
	}

	if (log_conf->mem_limit.lines_total > 0)
	{
		log_base->mem.lines_total_limit = log_conf->mem_limit.lines_total;
	}


	/* Intercept signals if we want a DUMP on a crash */
	if (log_conf->flags.dump_on_signal)
		EvKQBaseInterceptSignals(ev_base);

	/* AutoHash timer wont play nice with threads, for now */
	if (log_conf->flags.thread_safe)
		log_base->flags.autohash_disable = 1;

	/* Open file for logging if upper layers set */
	if (log_conf->fileout_pathstr)
	{
		log_base->fileout			= fopen(log_conf->fileout_pathstr, "a+");
		log_base->fileout_pathstr	= strdup(log_conf->fileout_pathstr);

		/* Unable to open for writing */
		if (log_base->fileout == NULL)
		{
			fprintf(stderr, "Failed opening LOG FILE [%s]\n", log_base->fileout_pathstr);
			free(log_base->fileout_pathstr);
			log_base->fileout_pathstr = NULL;
			return NULL;
		}

		EvKQBaseSetEvent(log_base->ev_base, log_base->fileout->_file, COMM_EV_FILEMON, COMM_ACTION_ADD_PERSIST, EvKQBaseLogBaseFileMonCB, log_base);

	}
	/* Will write just to STDERR */
	else
	{
		log_base->fileout_pathstr	= NULL;
		log_base->fileout			= NULL;
	}

	return log_base;
}
/**************************************************************************************************************************/
static int EvKQBaseLogBaseFileMonCB(int fd, int action, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBaseLogBase *log_base 	= cb_data;

	if (log_base->fileout)
		fclose(log_base->fileout);

	log_base->fileout			= fopen(log_base->fileout_pathstr, "a+");

	/* Unable to open for writing */
	if (log_base->fileout == NULL)
	{
		fprintf(stderr, "Failed opening LOG FILE [%s]\n", log_base->fileout_pathstr);
		return 1;
	}

	EvKQBaseSetEvent(log_base->ev_base, log_base->fileout->_file, COMM_EV_FILEMON, COMM_ACTION_ADD_PERSIST, EvKQBaseLogBaseFileMonCB, log_base);

    return 1;
}
/**************************************************************************************************************************/
int EvKQBaseLogBaseDestroy(EvKQBaseLogBase *log_base)
{
	/* Sanity check */
	if (!log_base)
		return 0;

	/* Set flags as destroyed */
	log_base->flags.destroyed = 1;

	/* Reference count still holds, bail out */
	if (log_base->ref_count-- > 0)
		return log_base->ref_count;

	/* Destroy log_base */
	EvKQBaseLogBaseDoDestroy(log_base);

	return 1;
}
/**************************************************************************************************************************/
EvKQBaseLogBase *EvKQBaseLogBaseLink(EvKQBaseLogBase *log_base)
{
	/* Sanity check */
	if (!log_base)
		return NULL;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (log_base->flags.thread_safe)
		MUTEX_LOCK(log_base->mutex, "LOG_BASE");

	log_base->ref_count++;

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (log_base->flags.thread_safe)
		MUTEX_UNLOCK(log_base->mutex, "LOG_BASE");

	return log_base;
}
/**************************************************************************************************************************/
int EvKQBaseLogBaseUnlink(EvKQBaseLogBase *log_base)
{
	/* Sanity check */
	if (!log_base)
		return 0;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (log_base->flags.thread_safe)
		MUTEX_LOCK(log_base->mutex, "LOG_BASE");

	/* Reference count still holds, bail out */
	if (log_base->ref_count-- > 0)
	{
		/* Running THREAD_SAFE, UNLOCK MUTEX */
		if (log_base->flags.thread_safe)
			MUTEX_UNLOCK(log_base->mutex, "LOG_BASE");

		return log_base->ref_count;
	}

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (log_base->flags.thread_safe)
		MUTEX_UNLOCK(log_base->mutex, "LOG_BASE");

	/* Destroy log_base */
	EvKQBaseLogBaseDoDestroy(log_base);

	return -1;
}
/**************************************************************************************************************************/
void EvKQBaseLoggerHexDump(EvKQBaseLogBase *log_base, int type, char *data, int size, int item_count, int column_count)
{
	char charstr[1024];
	char hexstr[1024];
	char bytestr[4];
	unsigned char cur_char;
	int n;

	unsigned char *p	= (unsigned char*)data;
	int line_size		= (item_count * column_count);
	int cur_color		= LOGCOLOR_YELLOW;

	/* Sanity check */
	if (!log_base)
		return;

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (log_base->flags.thread_safe)
		MUTEX_LOCK(log_base->mutex, "LOG_BASE");

	/* Clean buffers */
	memset(&bytestr, 0, sizeof(bytestr));
	memset(&hexstr, 0, sizeof(hexstr));
	memset(&charstr, 0, sizeof(charstr));

	/* Print lines */
	for (n = 1; n <= size; n++)
	{
		cur_char = *p;

		if (isalnum(cur_char) == 0)
		{
			cur_char = '.';
		}

		/* store hex str (for left side) */
		snprintf(bytestr, sizeof(bytestr), "%02X ", *p);
		strncat(hexstr, bytestr, sizeof(hexstr) - strlen(hexstr) - 1);

		/* store char str (for right side) */
		snprintf(bytestr, sizeof(bytestr), "%c", cur_char);
		strncat(charstr, bytestr, sizeof(charstr)-strlen(charstr)-1);

		memset(&bytestr, 0, sizeof(bytestr));

		/* line completed */
		if( n % line_size == 0)
		{
			EvKQBaseLoggerAddDontLock(log_base, type, cur_color, "", "", 0, "%s | %s\n", hexstr, charstr);

			memset(&hexstr, 0, sizeof(hexstr));
			memset(&charstr, 0, sizeof(charstr));

			/* Switch color */
			cur_color = ((cur_color + 1) % LOGCOLOR_LASTITEM);


		}
		/* Complete item add WHITE_SPACES */
		else if( n % item_count == 0)
		{
			strncat(hexstr, " ", sizeof(hexstr)-strlen(hexstr)-1);
			strncat(charstr, " ", sizeof(charstr)-strlen(charstr)-1);
		}

		p++; /* next byte */
	}

	/* Print any leftover of the buffer if its not empty */
	if (strlen(hexstr) > 0)
		EvKQBaseLoggerAddDontLock(log_base, type, cur_color, "", "", 0, "%-99.99s | %s\n", hexstr, charstr);

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (log_base->flags.thread_safe)
		MUTEX_UNLOCK(log_base->mutex, "LOG_BASE");

	return;

}
/**************************************************************************************************************************/
char *EvKQBaseLoggerAdd(EvKQBaseLogBase *log_base, int type, int color, const char *file, const char *func, const int line, const char *message, ...)
{
	/* Sanity check */
	if (!log_base)
		return NULL;

	/* Sanity check */
	if (type >= LOGTYPE_LASTITEM)
		return NULL;

	/* We are not keeping MEM LOGs, so if conditions are not met, leave now */
	if (!log_base->flags.mem_keep_logs)
	{
		/* This LOG_LEVEL do not trigger our LOG CONF, leave */
		if (log_base->log_level > type)
			return NULL;

		/* DEBUG log and DEBUG is explicitly DISABLED */
		if ((log_base->flags.debug_disable) && (LOGTYPE_DEBUG == type))
			return NULL;
	}

	/* Running THREAD_SAFE, LOCK MUTEX */
	if (log_base->flags.thread_safe)
		MUTEX_LOCK(log_base->mutex, "LOG_BASE");

	/* Set up STACK */
	EvKQBase *ev_base					= log_base->ev_base;
	EvKQBaseLogBase *_log_base			= ev_base->log_base;
	struct timeval *first_tv			= (struct timeval *)&ev_base->stats.first_invoke_tv;
	struct timeval *cur_tv				= (struct timeval *)&ev_base->stats.cur_invoke_tv;
	struct timeval uninit_tv 			= {0};
	struct tm *tm						= NULL;
	struct tm tm_tmp 					= {0};
	va_list args						= {0};
	static char time_buf[128] 			= {0};
	char fmt_buf[MEMBUFFER_MAX_PRINTF]	= {0};
	char lastmsg_buf[1024] 				= {0};
	char *buf_ptr						= (char*)&fmt_buf;
	char *time_buf_ptr					= (char*)&time_buf;
	char *lastmsg_buf_ptr				= (char*)&lastmsg_buf;
	char *hashstr_ptr					= NULL;
	char *color_str						= NULL;
	unsigned int lastmsg_hash 			= 0;
	int timer_id 						= log_base->lastmsg.timer_id;
	int msg_len 						= 0;
	int time_offset						= 0;
	int lastmsg_offset					= 0;
	int offset							= 0;
	int msg_malloc						= 0;
	int do_write						= 1;
	int alloc_sz						= MEMBUFFER_MAX_PRINTF;

	/* LOG_TYPE is LOWER than LOG_LEVEL */
	if (log_base->log_level > type)
		do_write = 0;

	/* DEBUG log and DEBUG is explicitly DISABLED */
	if ((log_base->flags.debug_disable) && (LOGTYPE_DEBUG == type))
		do_write = 0;

	/* Detach LOG_BASE from EV_BASE to avoid recursively looping on functions that use LogAdd */
	ev_base->log_base = NULL;

	/* Initialize CUR_TV */
	if (cur_tv->tv_sec == 0)
	{
		/* Touch time_stamps */
		gettimeofday(&uninit_tv, NULL);

		cur_tv	= &uninit_tv;
		tm		= localtime_r((const time_t*)&cur_tv->tv_sec, &tm_tmp);
	}
	/* Already initialized, grab from EV_BASE */
	else
		tm		= localtime_r((const time_t*)&ev_base->stats.cur_invoke_ts_sec, &tm_tmp);

	/* Generate TIME string */
	time_offset = strftime(time_buf, (sizeof(time_buf) - 1), "%H:%M:%S.", tm);
	snprintf((time_buf_ptr + time_offset), sizeof(time_buf), "%06ld - %06ld]", cur_tv->tv_usec, log_base->ev_base->stats.kq_invoke_count);

	/* Select COLOR */
	switch(color)
	{
	case LOGCOLOR_RED:		color_str = COLOR_FOREGROUND_LIGHTRED;		break;
	case LOGCOLOR_GREEN:	color_str = COLOR_FOREGROUND_LIGHTGREEN;	break;
	case LOGCOLOR_YELLOW:	color_str = COLOR_FOREGROUND_LIGHTYELLOW;	break;
	case LOGCOLOR_BLUE:		color_str = COLOR_FOREGROUND_LIGHTBLUE;		break;
	case LOGCOLOR_PURPLE:	color_str = COLOR_FOREGROUND_LIGHTPURPLE;	break;
	case LOGCOLOR_CYAN:		color_str = COLOR_FOREGROUND_LIGHTCYAN;		break;
	case LOGCOLOR_ORANGE:	color_str = COLOR_FOREGROUND_ORANGE;		break;

	default:				color_str = COLOR_FOREGROUND_DEFAULT;		break;
	}

	/* Create main MSG */
	va_start(args, message);
	msg_len = vsnprintf(NULL, 0, message, args);
	va_end(args);

	/* Too big to fit on local stack, use heap */
	if (msg_len > (MEMBUFFER_MAX_PRINTF - 16))
	{
		/* Set new alloc size to replace default */
		alloc_sz	= (msg_len + 16);
		buf_ptr		= malloc(alloc_sz);
		msg_malloc	= 1;
	}

	/* Initialize VA ARGs */
	va_start(args, message);
	offset = snprintf(buf_ptr, (alloc_sz - 1), "[%s-[%s]-[%s:%d]-[%s] - ", time_buf_ptr, evkq_glob_logtype_str[type], file, line, func);
	offset += vsnprintf((buf_ptr + offset), ((alloc_sz - 1) - offset), message, args);
	buf_ptr[offset] = '\0';

	/* Finish with VA ARGs list */
	va_end(args);

	/* AutoHash is not disabled, do it - Just hash what will be written */
	if ((!log_base->flags.autohash_disable) && do_write)
	{
		/* Calculate LASTMSG hash */
		hashstr_ptr	 = (buf_ptr + strlen(time_buf_ptr) + 1);
		lastmsg_hash = BrbSimpleHashStr(hashstr_ptr, offset, 4114);

		//printf("LAST [%u] | CUR [%u] | HASH_STR [%s]\n", lastmsg_hash, log_base->lastmsg.hash, hashstr_ptr);

		/* Last MSG is equal than current */
		if (lastmsg_hash == log_base->lastmsg.hash)
		{
			/* Add TIMER to check */
			if (log_base->lastmsg.timer_id < 0)
				log_base->lastmsg.timer_id = EvKQBaseTimerAdd(log_base->ev_base, COMM_ACTION_ADD_VOLATILE, 1000, EvKQBaseLoggerTimerEvent, log_base);

			/* Touch repeat count and re-link to LOG_BASE */
			log_base->lastmsg.count++;
			goto leave;
		}
		/* Last MSG is different than current */
		else
		{
			/* There are pending repeated messages, print them */
			if (log_base->lastmsg.count > 0)
			{
				/* Create LASTMSG */
				lastmsg_offset = snprintf(lastmsg_buf_ptr, sizeof(lastmsg_buf), "[%s-[REPEAT] - Last message repeated [%d] times\n", time_buf_ptr, log_base->lastmsg.count);
				lastmsg_buf_ptr[lastmsg_offset] = '\0';

				/* Write to log base */
				if ((!log_base->flags.mem_only_logs) && (do_write))
					EvKQBaseLogBaseDoWrite(log_base, COLOR_FOREGROUND_LIGHTGREEN, lastmsg_buf_ptr, lastmsg_offset);

				/* Upper layers turned ON in-memory logs - If we are running from inside a handler or crashing, do not do it, lists are probably broken */
				if ((log_base->flags.mem_keep_logs) && (!log_base->ev_base->flags.crashing_with_sig))
					EvKQBaseLogBaseMemLogAdd(log_base, COLOR_FOREGROUND_LIGHTGREEN, lastmsg_buf_ptr, lastmsg_offset);

				/* Cancel LASTMSG timer */
				if (log_base->lastmsg.timer_id > -1)
				{
					EvKQBaseTimerCtl(log_base->ev_base, timer_id, COMM_ACTION_DELETE);
					log_base->lastmsg.timer_id = -1;
				}
			}

			/* Update LASTMSG info */
			log_base->lastmsg.hash	= lastmsg_hash;
			log_base->lastmsg.count = 0;
		}
	}

	/* Save last message TV */
	memcpy(&log_base->lastmsg.tv, cur_tv, sizeof(struct timeval));

	/* Write to log base */
	if ((!log_base->flags.mem_only_logs) && (do_write))
		EvKQBaseLogBaseDoWrite(log_base, color_str, buf_ptr, offset);

	/* Upper layers turned ON in-memory logs - If we are running from inside a handler or crashing, do not do it, lists are probably broken */
	if ((log_base->flags.mem_keep_logs) && (!log_base->ev_base->flags.crashing_with_sig))
		EvKQBaseLogBaseMemLogAdd(log_base, color_str, buf_ptr, offset);

	/* Re-link LOG_BASE */
	leave:
	ev_base->log_base = _log_base;

	/* Used MALLOC< release it */
	if (msg_malloc)
		free(buf_ptr);

	/* Running THREAD_SAFE, UNLOCK MUTEX */
	if (log_base->flags.thread_safe)
		MUTEX_UNLOCK(log_base->mutex, "LOG_BASE");

	return buf_ptr;

}
/**************************************************************************************************************************/
int EvKQBaseLoggerMemDumpOnCrash(EvKQBaseLogBase *log_base)
{
	char file_buf[512];
	int offset;

	//	printf("DUMPING CRASH - LOG [%p]\n", log_base);

	/* No log base */
	if (!log_base)
		return 0;

	//	printf("DUMPING CRASH - DUMP ON SIGNAL [%d]\n", log_base->flags.dump_on_signal);

	/* Do not want to DUMP on signal */
	if (!log_base->flags.dump_on_signal)
		return 0;

	/* Disable MEM_KEEPING to avoid unwanted lines */
	log_base->flags.mem_only_logs = 0;
	log_base->flags.mem_keep_logs = 0;

	/* Create FILE_PATH */
	if (log_base->fileout_pathstr)
	{
		offset = snprintf((char *)&file_buf, (sizeof(file_buf) - 1), "%s-DUMP-%d", log_base->fileout_pathstr, getpid());
		file_buf[offset] = '\0';
	}
	else
	{
		offset = snprintf((char *)&file_buf, (sizeof(file_buf) - 1), "DUMP-%d", getpid());
		file_buf[offset] = '\0';
	}

	KQBASE_LOG_PRINTF(log_base, LOGTYPE_CRITICAL, LOGCOLOR_YELLOW, "LOG - Dumping [%d] bytes on [%d] lines to file [%s]\n",
			log_base->mem.bytes_total_cur, log_base->mem.lines_total_cur, file_buf);

	/* Write DATA */
	EvKQBaseLoggerMemDumpToFile(log_base, (char*)&file_buf);
	return 1;
}
/**************************************************************************************************************************/
int EvKQBaseLoggerMemDumpToFile(EvKQBaseLogBase *log_base, char *path_str)
{
	EvKQBaseLogMemEntry *log_entry;
	DLinkedListNode *node;
	FILE *fileout;
	int loop_safe_limit;
	int line_count;

	//	printf("DUMPING TO FILE - LOG [%p] - FILE [%s]\n", log_base, path_str);

	/* Sanity check */
	if (!log_base)
		return 0;

	/* Open destination file */
	fileout = fopen(path_str, "a+");

	//	printf("DUMPING TO FILE - LOG [%p] - FILEOUT [%p]\n", log_base, fileout);

	/* Failed to open file */
	if (!fileout)
		return 0;

	loop_safe_limit = (log_base->mem.list.size + 1);

	/* Limit loop count as we may be broken */
	if (loop_safe_limit > log_base->mem.lines_total_limit)
		loop_safe_limit = (log_base->mem.lines_total_limit + 1);

	/* Dump all lines in memory - Lets take extra care with this list, because we can be running from a signal handler thread and it should be broken! */
	for (line_count = 0, node = log_base->mem.list.tail; (node && loop_safe_limit >= 0); (node = node->prev, line_count++, loop_safe_limit--))
	{
		log_entry = node->data;

		/* Dump line and move on */
		if (!log_entry->color_str)
			fwrite(log_entry->color_str, strlen(log_entry->color_str), 1, fileout);

		fwrite(log_entry->line_str, log_entry->line_sz, 1, fileout);

		if (!log_base->flags.disable_colors_onfile)
			fwrite(COLOR_FOREGROUND_DEFAULT, strlen(COLOR_FOREGROUND_DEFAULT), 1, fileout);

		continue;
	}

	/* Sync and close */
	fflush(fileout);
	fclose(fileout);

	return line_count;
}
/**************************************************************************************************************************/
int EvKQBaseLoggerMemDump(EvKQBaseLogBase *log_base)
{
	EvKQBaseLogMemEntry *log_entry;
	DLinkedListNode *node;
	int loop_safe_limit;
	int line_count;

	/* Sanity check */
	if (!log_base)
		return 0;

	loop_safe_limit = (log_base->mem.list.size + 1);

	/* Limit loop count as we may be broken */
	if (loop_safe_limit > 8092)
		loop_safe_limit = 8092;

	/* Dump all lines in memory - Lets take extra care with this list, because we can be running from a signal handler thread and it should be broken! */
	for (line_count = 0, node = log_base->mem.list.tail; (node && loop_safe_limit >= 0); (node = node->prev, line_count++, loop_safe_limit--))
	{
		log_entry = node->data;

		/* Dump line and move on */
		EvKQBaseLogBaseDoWrite(log_base, log_entry->color_str, log_entry->line_str, log_entry->line_sz);
		continue;
	}

	return line_count;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
/* Debug functions
/**************************************************************************************************************************/
void EvKQBaseDumpKEvent(struct kevent *kev)
{
	static const char *nam[] = {"EV_ADD", "EV_ENABLE", "EV_DISABLE", "EV_DELETE", "EV_ONESHOT", "EV_CLEAR", "EV_EOF", "EV_ERROR", NULL };
	static const char *evfilt_nam[] = {"EVFILT_READ", "EVFILT_WRITE", "EVFILT_TIMER", "EVFILT_AIO", "EVFILT_VNODE", "EVFILT_PROC", "EVFILT_SIGNAL",	NULL };
	static const int evfilt_val[] = {EVFILT_READ, EVFILT_WRITE, EVFILT_TIMER, EVFILT_AIO, EVFILT_VNODE, EVFILT_PROC, EVFILT_SIGNAL,	0 };
	static const int val[] = {EV_ADD, EV_ENABLE, EV_DISABLE, EV_DELETE, EV_ONESHOT, EV_CLEAR, EV_EOF, EV_ERROR, 0 };

	int i;

	fprintf(stderr, "kevent: ident=%d filter=", (int) kev->ident);

	for (i = 0; evfilt_val[i] != 0; i++)
	{
		if (kev->filter == evfilt_val[i])
		{
			fprintf(stderr, "%s ", evfilt_nam[i]);
			break;
		}
	}

	fprintf(stderr, "flags=");

	for (i = 0; val[i] != 0; i++)
	{
		if (kev->flags & val[i])
			fprintf(stderr, "%s ", nam[i]);
	}

	fprintf(stderr, "udata=%p", kev->udata);
	fprintf(stderr, "\n");

	return;
}
/**************************************************************************************************************************/
void EvKQBaseLogHexDump(char *data, int size, int item_count, int column_count)
{

	char charstr[1024];
	char hexstr[1024];
	char bytestr[4];
	unsigned char cur_char;
	int n;

	unsigned char *p	= (unsigned char*)data;
	int line_size		= (item_count * column_count);

	/* Clean buffers */
	memset(&bytestr, 0, sizeof(bytestr));
	memset(&hexstr, 0, sizeof(hexstr));
	memset(&charstr, 0, sizeof(charstr));

	/* Print lines */
	for (n = 1; n <= size; n++)
	{
		cur_char = *p;

		if (isalnum(cur_char) == 0)
		{
			cur_char = '.';
		}

		/* store hex str (for left side) */
		snprintf(bytestr, sizeof(bytestr), "%02X ", *p);
		strncat(hexstr, bytestr, sizeof(hexstr) - strlen(hexstr) - 1);

		/* store char str (for right side) */
		snprintf(bytestr, sizeof(bytestr), "%c", cur_char);
		strncat(charstr, bytestr, sizeof(charstr)-strlen(charstr)-1);

		memset(&bytestr, 0, sizeof(bytestr));

		/* line completed */
		if( n % line_size == 0)
		{
			printf("%s | %s\n", hexstr, charstr);

			memset(&hexstr, 0, sizeof(hexstr));
			memset(&charstr, 0, sizeof(charstr));


		}
		/* Complete item add WHITE_SPACES */
		else if( n % item_count == 0)
		{
			strncat(hexstr, " ", sizeof(hexstr)-strlen(hexstr)-1);
			strncat(charstr, " ", sizeof(charstr)-strlen(charstr)-1);
		}

		p++; /* next byte */
	}

	/* TODO: calculate characters per line using col_sz */
	/* Print any leftover of the buffer if its not empty */
	if (strlen(hexstr) > 0)
		printf("%-99.99s | %s\n", hexstr, charstr);

}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static EvKQBaseLogMemEntry *EvKQBaseLogBaseMemLogAdd(EvKQBaseLogBase *log_base, char *color_str, char *line_str, int line_sz)
{
	EvKQBaseLogMemEntry *log_entry;

	/* Upper layers turned off MEM logging */
	if (!log_base->flags.mem_keep_logs)
		return NULL;

	/* No limit set and not explicitly set to unlimited, bail out */
	if (!log_base->flags.mem_unlimited && ((log_base->mem.bytes_total_limit <= 0) || (log_base->mem.lines_total_limit <= 0)))
		return NULL;

	/* Create new entry and add to LIST */
	log_entry = calloc(1, sizeof(EvKQBaseLogMemEntry));
	DLinkedListAdd(&log_base->mem.list, &log_entry->node, log_entry);

	/* Populate entry */
	log_entry->color_str	= color_str;
	log_entry->line_str		= strdup(line_str);
	log_entry->line_sz		= strlen(line_str);

	memcpy(&log_entry->tv_add, &log_base->ev_base->stats.cur_invoke_tv, sizeof(struct timeval));

	/* Touch counters */
	log_base->mem.bytes_total_cur += log_entry->line_sz;
	log_base->mem.lines_total_cur++;

	/* Enforce memory usage limits */
	EvKQBaseLogBaseMemEnforceLimit(log_base);

	return log_entry;
}
/**************************************************************************************************************************/
static int EvKQBaseLogBaseMemLogDelete(EvKQBaseLogBase *log_base, EvKQBaseLogMemEntry *log_entry)
{
	/* Sanity check */
	if (!log_entry)
		return 0;

	/* Touch counters */
	log_base->mem.bytes_total_cur -= log_entry->line_sz;
	log_base->mem.lines_total_cur--;

	/* Delete from LIST and free entry */
	DLinkedListDelete(&log_base->mem.list, &log_entry->node);
	free(log_entry->line_str);
	free(log_entry);

	return 1;
}
/**************************************************************************************************************************/
static int EvKQBaseLogBaseMemEnforceLimit(EvKQBaseLogBase *log_base)
{
	EvKQBaseLogMemEntry *log_entry;

	//DLinkedListNode *tail_node	= log_base->mem.list.tail;
	int delete_count			= 0;

	assert(log_base);

	//printf ("enforce limit - line [%d] - size [%d]\n", log_base->mem.lines_total_cur, log_base->mem.bytes_total_cur);

	/* Memory size limit reached in bytes */
	if ((log_base->mem.bytes_total_limit > 0) && (log_base->mem.bytes_total_cur >= log_base->mem.bytes_total_limit))
	{
		/* Delete until limit is honored */
		while (log_base->mem.bytes_total_cur >= log_base->mem.bytes_total_limit)
		{
			/* Pop log entry from tail */
			log_entry = DLinkedListPopTail(&log_base->mem.list);

			/* Empty tail, bail out */
			if (!log_entry)
				return delete_count;

			/* Delete entry and increment count */
			EvKQBaseLogBaseMemLogDelete(log_base, log_entry);
			delete_count++;
			continue;
		}
	}

	/* Line count limit reached in bytes */
	if ((log_base->mem.lines_total_limit > 0) && (log_base->mem.lines_total_cur >= log_base->mem.lines_total_limit))
	{
		/* Delete until limit is honored */
		while (log_base->mem.lines_total_cur >= log_base->mem.lines_total_limit)
		{
			/* Pop log entry from tail */
			log_entry = DLinkedListPopTail(&log_base->mem.list);

			/* Empty tail, bail out */
			if (!log_entry)
				return delete_count;

			/* Delete entry and increment count */
			EvKQBaseLogBaseMemLogDelete(log_base, log_entry);
			delete_count++;
			continue;
		}
	}

	return delete_count;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int EvKQBaseLogBaseDoWrite(EvKQBaseLogBase *log_base, char *color_str, char *line_str, int line_sz)
{
	int op_status;

	/* Sanity check */
	if (!log_base)
		return 0;

	/* There is a file SET, write to IT */
	if (log_base->fileout)
	{
		if (!log_base->flags.disable_colors_onfile)
			fwrite(color_str, strlen(color_str), 1, log_base->fileout);

		fwrite(line_str, line_sz, 1, log_base->fileout);
		fflush(log_base->fileout);

		if (!log_base->flags.disable_colors_onfile)
			fwrite(COLOR_FOREGROUND_DEFAULT, strlen(COLOR_FOREGROUND_DEFAULT), 1, log_base->fileout);
	}
	/* Write to STDERR */
	else
	{
		op_status = write(KQBASE_STDERR, color_str, strlen(color_str));
		op_status = write(KQBASE_STDERR, line_str, line_sz);
		op_status = write(KQBASE_STDERR, COLOR_FOREGROUND_DEFAULT, strlen(COLOR_FOREGROUND_DEFAULT));
	}

	/* Upper layers has file SET but wants DOUBLE WRITE, write to STDERR too */
	if ((log_base->fileout) && (log_base->flags.double_write))
	{
		op_status = write(KQBASE_STDERR, color_str, strlen(color_str));
		op_status = write(KQBASE_STDERR, line_str, line_sz);
		op_status = write(KQBASE_STDERR, COLOR_FOREGROUND_DEFAULT, strlen(COLOR_FOREGROUND_DEFAULT));
	}

	return 1;
}
/**************************************************************************************************************************/
static int EvKQBaseLogBaseDoDestroy(EvKQBaseLogBase *log_base)
{
	/* There should be no locks here */
	assert(-1 == log_base->ref_count);

	/* Delete and RESET timer ID */
	if (log_base->lastmsg.timer_id > -1)
		EvKQBaseTimerCtl(log_base->ev_base, log_base->lastmsg.timer_id, COMM_ACTION_DELETE);
	log_base->lastmsg.timer_id = -1;

	/* Close open file left */
	if (log_base->fileout)
	{
		fclose(log_base->fileout);
		free(log_base->fileout_pathstr);

		log_base->fileout_pathstr	= NULL;
		log_base->fileout			= NULL;
	}


	/* Running thread safe, destroy MUTEX */
	if (log_base->flags.thread_safe)
		MUTEX_DESTROY(log_base->mutex, "LOG_BASE");

	/* Destroy base */
	free(log_base);

	return 1;
}
/**************************************************************************************************************************/
static void EvKQBaseLoggerAddDontLock(EvKQBaseLogBase *log_base, int type, int color, const char *file, const char *func, const int line, const char *message, ...)
{
	char buf[KQBASE_LOGGER_MAX_LOGLINE_SZ] 	= {0};
	static char time_buf[128] 				= {0};
	char *color_str 						= NULL;
	va_list args;

	/* Sanity check */
	if (!log_base)
		return;

	if (type >= LOGTYPE_LASTITEM)
		return;

	if (log_base->log_level > type)
		return;

	struct timeval *first_tv	= (struct timeval *)&log_base->ev_base->stats.first_invoke_tv;
	struct timeval *cur_tv		= (struct timeval *)&log_base->ev_base->stats.cur_invoke_tv;
	struct tm *tm				= localtime((const time_t*)&log_base->ev_base->stats.cur_invoke_ts_sec);
	char *buf_ptr				= (char*)&buf;
	char *time_buf_ptr			= (char*)&time_buf;
	int time_offset				= 0;
	int offset					= 0;

	snprintf((time_buf_ptr + time_offset), sizeof(time_buf), "%06ld - %06ld]", cur_tv->tv_usec, log_base->ev_base->stats.kq_invoke_count);

	switch(color)
	{
	case LOGCOLOR_RED:		color_str = COLOR_FOREGROUND_LIGHTRED;		break;
	case LOGCOLOR_GREEN:	color_str = COLOR_FOREGROUND_LIGHTGREEN;	break;
	case LOGCOLOR_YELLOW:	color_str = COLOR_FOREGROUND_LIGHTYELLOW;	break;
	case LOGCOLOR_BLUE:		color_str = COLOR_FOREGROUND_LIGHTBLUE;		break;
	case LOGCOLOR_PURPLE:	color_str = COLOR_FOREGROUND_LIGHTPURPLE;	break;
	case LOGCOLOR_CYAN:		color_str = COLOR_FOREGROUND_LIGHTCYAN;		break;
	default:				color_str = COLOR_FOREGROUND_DEFAULT;		break;
	}

	va_start(args, message);
	offset = snprintf(buf_ptr, sizeof(buf), "[%s-[%s]-[%s:%d]-[%s] - ", time_buf_ptr, evkq_glob_logtype_str[type], file, line, func);
	offset += vsnprintf((buf_ptr + offset), (sizeof(buf) - offset), message, args);
	buf_ptr[offset] = '\0';
	va_end(args);

	/* NULL terminate the thing */
	buf_ptr[offset] = '\0';

	/* Write to log base */
	if (!log_base->flags.mem_only_logs)
		EvKQBaseLogBaseDoWrite(log_base, color_str, buf_ptr, offset);

	/* Upper layers turned ON in-memory logs - If we are running from inside a handler or crashing, do not do it, lists are probably broken */
	if ((log_base->flags.mem_keep_logs) && (!log_base->ev_base->flags.crashing_with_sig))
		EvKQBaseLogBaseMemLogAdd(log_base, color_str, buf_ptr, offset);

	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int EvKQBaseLoggerTimerEvent(int timer_id, int unused, int thrd_id, void *cb_data, void *base_ptr)
{
	char lastmsg_buf[1024];
	char time_buf[512];

	EvKQBaseLogBase *log_base	= cb_data;
	char *time_buf_ptr			= (char*)&time_buf;
	char *lastmsg_buf_ptr		= (char*)&lastmsg_buf;
	struct tm *tm				= localtime((const time_t*)&log_base->ev_base->stats.cur_invoke_ts_sec);
	int lastmsg_delta			= EvKQBaseTimeValSubMsec(&log_base->lastmsg.tv, &log_base->ev_base->stats.cur_invoke_tv);
	int lastmsg_offset			= 0;
	int time_offset				= 0;

	/* Reset TIMER_ID */
	log_base->lastmsg.timer_id = -1;

	/* There are no pending repeated messages, bail out */
	if (log_base->lastmsg.count == 0)
		return 0;

	/* Not initialized */
	if (log_base->lastmsg.tv.tv_sec == 0)
		return 0;

	/* Last delta too big, print repeated messages */
	if (lastmsg_delta > 1000)
	{
		/* Generate TIME string */
		time_offset = strftime(time_buf, (sizeof(time_buf) - 1), "%H:%M:%S.", tm);
		snprintf((time_buf_ptr + time_offset), sizeof(time_buf), "%06ld - %06ld]", log_base->ev_base->stats.cur_invoke_tv.tv_usec, log_base->ev_base->stats.kq_invoke_count);

		/* Create LASTMSG */
		lastmsg_offset = snprintf(lastmsg_buf_ptr, sizeof(lastmsg_buf), "[%s-[REPEAT] - Last message repeated [%d] times\n", time_buf_ptr, log_base->lastmsg.count);
		lastmsg_buf_ptr[lastmsg_offset] = '\0';

		/* Write to log base */
		if (!log_base->flags.mem_only_logs)
			EvKQBaseLogBaseDoWrite(log_base, COLOR_FOREGROUND_LIGHTGREEN, lastmsg_buf_ptr, lastmsg_offset);

		/* Update TV to NOW */
		memcpy(&log_base->lastmsg.tv, &log_base->ev_base->stats.cur_invoke_tv, sizeof(struct timeval));

		/* Update LASTMSG info */
		log_base->lastmsg.hash	= 0;
		log_base->lastmsg.count = 0;

		return 1;
	}

	/* TAG to reschedule and leave */
	reschedule:
	log_base->lastmsg.timer_id = EvKQBaseTimerAdd(log_base->ev_base, COMM_ACTION_ADD_VOLATILE, 1000, EvKQBaseLoggerTimerEvent, log_base);
	return 1;
}
