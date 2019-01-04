/*
 * libbrb_ev_logger.h
 *
 *  Created on: 2014-09-12
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

#ifndef LIBBRB_LOGGER_H_
#define LIBBRB_LOGGER_H_

#define KQBASE_LOG_PRINTF(log_base, type, color, msg, ...)	if (log_base) EvKQBaseLoggerAdd(log_base, type, color, __FILE__, __func__, __LINE__, msg, ##__VA_ARGS__)

/******************************************************************************************************/
/* Background Colors
/******************************************************************************************************/
#define COLOR_BACKGROUND_BLACK 			"\033[40m"
#define COLOR_BACKGROUND_RED   			"\033[41m"
#define COLOR_BACKGROUND_GREEN 			"\033[42m"
#define COLOR_BACKGROUND_YELLOW 		"\033[43m"
#define COLOR_BACKGROUND_BLUE  			"\033[44m"
#define COLOR_BACKGROUND_MAGENTA 		"\033[45m"
#define COLOR_BACKGROUND_CYAN 			"\033[46m"
#define COLOR_BACKGROUND_GREY 			"\033[47m"
/******************************************************************************************************/
/* Foreground Colors
/******************************************************************************************************/
#define COLOR_FOREGROUND_DEFAULT 		"\033[0m"
#define COLOR_FOREGROUND_BLACK 			"\033[30m"
#define COLOR_FOREGROUND_RED   			"\033[31m"
#define COLOR_FOREGROUND_GREEN 			"\033[32m"
#define COLOR_FOREGROUND_YELLOW 		"\033[33m"
#define COLOR_FOREGROUND_BLUE  			"\033[34m"
#define COLOR_FOREGROUND_PURPLE 		"\033[35m"
#define COLOR_FOREGROUND_CYAN 			"\033[36m"
#define COLOR_FOREGROUND_GREY 			"\033[37m"
#define COLOR_FOREGROUND_WHITE 			"\033[1m"
#define COLOR_FOREGROUND_ORANGE			"\033[38;5;202m"
/******************************************************************************************************/
#define COLOR_FOREGROUND_DARKGRAY 		"\033[01;30m"
#define COLOR_FOREGROUND_LIGHTRED 		"\033[01;31m"
#define COLOR_FOREGROUND_LIGHTGREEN 	"\033[01;32m"
#define COLOR_FOREGROUND_LIGHTYELLOW 	"\033[01;33m"
#define COLOR_FOREGROUND_LIGHTBLUE 		"\033[01;34m"
#define COLOR_FOREGROUND_LIGHTPURPLE 	"\033[01;35m"
#define COLOR_FOREGROUND_LIGHTCYAN 		"\033[01;36m"
/******************************************************************************************************/

typedef enum
{
	LOGTYPE_UNINIT,
	LOGTYPE_VERBOSE,
	LOGTYPE_INFO,
	LOGTYPE_WARNING,
	LOGTYPE_DEBUG,
	LOGTYPE_CRITICAL,
	LOGTYPE_LASTITEM
} EvKQBaseLogTypeCodes;

typedef enum
{
	LOGCOLOR_RED,
	LOGCOLOR_GREEN,
	LOGCOLOR_YELLOW,
	LOGCOLOR_BLUE,
	LOGCOLOR_PURPLE,
	LOGCOLOR_CYAN,
	LOGCOLOR_ORANGE,
	LOGCOLOR_LASTITEM
} EvKQBaseLogColorCodes;

typedef struct _EvKQBaseLogMemEntry
{
	DLinkedListNode node;
	struct timeval tv_add;
	char *color_str;
	char *line_str;
	int line_sz;

	struct
	{
		unsigned int foo:1;
	} flags;

} EvKQBaseLogMemEntry;

typedef struct _EvKQBaseLogBaseConf
{
	char *fileout_pathstr;
	int log_level;
	int log_section;

	struct
	{
		long bytes_total;
		long lines_total;
	} mem_limit;

	struct
	{
		unsigned int disable_colors_onfile:1;
		unsigned int double_write:1;
		unsigned int mem_only_logs:1;
		unsigned int mem_keep_logs:1;
		unsigned int mem_unlimited:1;
		unsigned int thread_safe:1;
		unsigned int debug_disable:1;
		unsigned int autohash_disable:1;
		unsigned int dump_on_signal:1;
		unsigned int destroyed:1;
	} flags;

} EvKQBaseLogBaseConf;

typedef struct _EvKQBaseLogBase
{
	struct _EvKQBase *ev_base;
	pthread_mutex_t mutex;
	FILE *fileout;
	char *fileout_pathstr;
	int ref_count;
	int log_level;
	int log_section;

	struct
	{
		struct timeval tv;
		unsigned int hash;
		unsigned int count;
		int timer_id;
	} lastmsg;

	struct
	{
		DLinkedList list;
		long bytes_total_cur;
		long lines_total_cur;
		long bytes_total_limit;
		long lines_total_limit;
	} mem;

	struct
	{
		unsigned int disable_colors_onfile:1;
		unsigned int double_write:1;
		unsigned int mem_only_logs:1;
		unsigned int mem_keep_logs:1;
		unsigned int mem_unlimited:1;
		unsigned int thread_safe:1;
		unsigned int debug_disable:1;
		unsigned int autohash_disable:1;
		unsigned int dump_on_signal:1;
		unsigned int destroyed:1;
	} flags;

} EvKQBaseLogBase;


static const char *evkq_glob_logtype_str[] = {"LOGTYPE_UNINIT", "VERBOSE", "INFO", "WARNING", "DEBUG", "CRITICAL", NULL};

/* ev_kq_logger.c */
EvKQBaseLogBase *EvKQBaseLogBaseNew(struct _EvKQBase *ev_base, EvKQBaseLogBaseConf *log_conf);
int EvKQBaseLogBaseDestroy(EvKQBaseLogBase *log_base);
EvKQBaseLogBase *EvKQBaseLogBaseLink(EvKQBaseLogBase *log_base);
int EvKQBaseLogBaseUnlink(EvKQBaseLogBase *log_base);
void EvKQBaseLoggerHexDump(EvKQBaseLogBase *log_base, int type, char *data, int size, int item_count, int column_count);
char *EvKQBaseLoggerAdd(EvKQBaseLogBase *log_base, int type, int color, const char *file, const char *func, const int line, const char *message, ...);
void EvKQBaseDumpKEvent(struct kevent *kev);
void EvKQBaseLogHexDump(char *data, int size, int item_count, int column_count);
int EvKQBaseLoggerMemDumpOnCrash(EvKQBaseLogBase *log_base);
int EvKQBaseLoggerMemDumpToFile(EvKQBaseLogBase *log_base, char *path_str);
int EvKQBaseLoggerMemDump(EvKQBaseLogBase *log_base);


#endif /* LIBBRB_LOGGER_H_ */
