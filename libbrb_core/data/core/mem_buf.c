/*
 * mem_buffer.c
 *
 *  Created on: 2011-10-12
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2011 BrByte Software (Oliveira Alves & Amorim LTDA)
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

static void MemBufferCheckForGrow(MemBuffer *mb_ptr, unsigned long new_data_sz);
static int MemBufferDoDestroy(MemBuffer *mb_ptr);

/**************************************************************************************************************************/
MemBuffer *MemBufferNew(LibDataThreadSafeType mb_type, int grow_rate)
{
	MemBuffer *mb_ptr;

	BRB_CALLOC(mb_ptr, 1, sizeof(MemBuffer));

	mb_ptr->busy_flags	= 0;
	mb_ptr->size		= 0;
	mb_ptr->capacity	= 0;

	/* Initialize it as NULL */
	mb_ptr->data = NULL;

	/* Set array type into structure */
	mb_ptr->mb_type = mb_type;

	/* If its a multithreaded safe dyn array, initialize mutex */
	if (mb_type == BRBDATA_THREAD_SAFE)
	{
		/* Initialize dyn array mutex */
		pthread_mutex_init(&mb_ptr->mutex, NULL);

		/* Set DynArray object status as free */
		EBIT_SET(mb_ptr->busy_flags, MEMBUFFER_FREE);
	}

	/* grow_rate = 0 means we want default value */
	if (grow_rate == 0)
		mb_ptr->grow_rate = 64;
	else
		mb_ptr->grow_rate = grow_rate;

	return mb_ptr;

}
/**************************************************************************************************************************/
int MemBufferDestroy(MemBuffer *mb_ptr)
{
	/* Sanity checks */
	if (!mb_ptr)
		return 0;

	/* Set flags as destroyed */
	mb_ptr->flags.destroyed = 1;

	/* Reference count still holds, bail out */
	if (mb_ptr->ref_count-- > 0)
		return mb_ptr->ref_count;

	MemBufferDoDestroy(mb_ptr);

	return -1;
}
/**************************************************************************************************************************/
int MemBufferClean(MemBuffer *mb_ptr)
{
	/* Sanity checks */
	if (!mb_ptr)
		return 0;

	/* CRITICAL SECTION - BEGIN */
	if (mb_ptr->mb_type == BRBDATA_THREAD_SAFE)
		_MemBufferEnterCritical(mb_ptr);

	/* Clean it all */
	memset(mb_ptr->data, 0, mb_ptr->size);

	/* New size is zero. Reset offset. Keep the same capacity */
	mb_ptr->size		= 0;
	mb_ptr->offset		= 0;

	/* Release WIRE, if WIRED */
	MemBufferUnwirePages(mb_ptr);

	/* CRITICAL SECTION - END */
	if (mb_ptr->mb_type == BRBDATA_THREAD_SAFE)
		_MemBufferLeaveCritical(mb_ptr);

	return 1;
}
/**************************************************************************************************************************/
int MemBufferShrink(MemBuffer *mb_ptr)
{
	/* Sanity checks */
	if (!mb_ptr)
		return 0;

	/* Better safe than sorry */
	MemBufferPutNULLTerminator(mb_ptr);

	/* Shrink buffer size to smallest possible */
	BRB_REALLOC(mb_ptr->data, mb_ptr->data, (mb_ptr->size + 1) * sizeof(char));

	/* Set new capacity */
	mb_ptr->capacity = mb_ptr->size;

	/* Remove from COREDUMPs */
	if (mb_ptr->flags.no_core)
		MemBufferNoCoreOnCrash(mb_ptr);

	/* RESET WIRE_MEM if WIRED */
	if (mb_ptr->flags.wired)
	{
		/* Reset WIRE for pages */
		MemBufferUnwirePages(mb_ptr);
		MemBufferWirePages(mb_ptr);
	}

	return 1;
}
/**************************************************************************************************************************/
int MemBufferLock(MemBuffer *mb_ptr)
{
	/* Sanity checks */
	if (!mb_ptr)
		return 0;

	mb_ptr->ref_count++;

	return mb_ptr->ref_count;
}
/**************************************************************************************************************************/
int MemBufferUnlock(MemBuffer *mb_ptr)
{
	/* Sanity checks */
	if (!mb_ptr)
		return 0;

	/* Reference count still holds, bail out */
	if (mb_ptr->ref_count-- > 0)
		return mb_ptr->ref_count;

	MemBufferDoDestroy(mb_ptr);

	return -1;
}
/**************************************************************************************************************************/
unsigned long MemBufferGetSize(MemBuffer *mb_ptr)
{
	if (!mb_ptr)
		return 0;

	return mb_ptr->size - mb_ptr->offset;
}
/**************************************************************************************************************************/
unsigned long MemBufferGetCapacity(MemBuffer *mb_ptr)
{
	if (!mb_ptr)
		return 0;

	return mb_ptr->capacity;
}
/**************************************************************************************************************************/
int MemBufferShrinkToToken(MemBuffer *mb_ptr, char token)
{
	char *data_ptr;
	int data_sz;
	int i, j;

	int token_found 	= 0;

	/* Sanity check */
	if (!mb_ptr)
		return 0;

	data_ptr	= MemBufferDeref(mb_ptr);
	data_sz		= MemBufferGetSize(mb_ptr);

	/* Search token inside MB */
	for (j = 0, i = data_sz; i > 0; j++, i--)
	{
		if (token == data_ptr[i])
		{
			token_found = 1;
			j--;
			break;
		}

		continue;
	}

	/* Token not found, leave */
	if (!token_found)
		return 0;

	/* Little dirty hack to adjust position to token */
	mb_ptr->size -= j;
	assert(mb_ptr->size >= 0);
	return j;
}
/**************************************************************************************************************************/
char* MemBufferLastCharGrab(MemBuffer *mb_ptr)
{
	char *base_ptr;

	/* Sanity check */
	if (!mb_ptr)
		return "";

	if (0 == mb_ptr->size)
		return "";

	base_ptr	= mb_ptr->data;
	base_ptr	+= (mb_ptr->size - 1);

	return base_ptr;
}
/**************************************************************************************************************************/
void MemBufferRemoveLastChar(MemBuffer *mb_ptr)
{
	char *base_ptr;

	/* Sanity check */
	if (!mb_ptr)
		return;

	if (0 == mb_ptr->size)
		return;

	if (1 == mb_ptr->size)
		MemBufferClean(mb_ptr);
	else
	{
		base_ptr	= mb_ptr->data;
		base_ptr	+= (mb_ptr->size - 1);
		base_ptr[0] = '\0';

		mb_ptr->size--;
	}

	return;
}
/**************************************************************************************************************************/
void MemBufferPutNULLTerminator(MemBuffer *mb_ptr)
{
	if (!mb_ptr)
		return;

	/* Little hack to make sure MEM_BUF is NULL terminated */
	MemBufferAdd(mb_ptr, "\0", 1);
	mb_ptr->size--;


	return;
}
/**************************************************************************************************************************/
int MemBufferIsInit(MemBuffer *mb_ptr)
{
	return (mb_ptr->capacity > 0);
}
/**************************************************************************************************************************/
MemBuffer *MemBufferDup(MemBuffer *mb_ptr)
{
	MemBuffer *new_mb;
	void *data_ptr			= NULL;
	unsigned long data_sz	= 0;

	/* Sanity check */
	if (!mb_ptr)
		return 0;

	/* Get ptr to data */
	data_ptr = MemBufferDeref(mb_ptr);

	/* Get data sz */
	data_sz = MemBufferGetSize(mb_ptr);

	/* Create the new mem_buf */
	new_mb = MemBufferNew(mb_ptr->mb_type, (data_sz + 1) );

	/* Add data into new_mb */
	MemBufferAdd(new_mb, data_ptr, data_sz);
	new_mb->offset = mb_ptr->offset;

	return new_mb;
}
/**************************************************************************************************************************/
MemBuffer *MemBufferDupOffset(MemBuffer *mb_ptr, unsigned long offset)
{
	MemBuffer *new_mb;
	void *data_ptr			= NULL;
	unsigned long data_sz	= 0;

	/* Sanity check */
	if (!mb_ptr)
		return 0;

	/* Get pointer to data and get correct size */
	data_ptr 	= MemBufferOffsetDeref(mb_ptr, offset);
	data_sz 	= MemBufferGetSize(mb_ptr) - offset;

	if (data_sz <= 0)
		return NULL;

	/* Create the new mem_buf */
	new_mb = MemBufferNew(mb_ptr->mb_type, (data_sz + 1) );

	/* Add data into new_mb */
	MemBufferAdd(new_mb, data_ptr, data_sz);

	return new_mb;
}
/**************************************************************************************************************************/
int MemBufferAppendNULL(MemBuffer *mb_ptr)
{

	/* Sanity check */
	if (!mb_ptr)
		return 0;

	/* Append NULL */
	MemBufferAdd(mb_ptr, "", 1);

	return 1;
}
/**************************************************************************************************************************/
unsigned long MemBufferOverwrite(MemBuffer *mb_ptr, void *new_data, unsigned long new_data_offset, unsigned long new_data_sz)
{
	unsigned long needed_size	= (new_data_offset + new_data_sz);
	void *base_ptr				= NULL;

	/* Sanity checks */
	if (!mb_ptr)
		return 0;

	if (mb_ptr->flags.readonly)
		return 0;

	/* CRITICAL SECTION - BEGIN */
	if (mb_ptr->mb_type == BRBDATA_THREAD_SAFE)
		_MemBufferEnterCritical(mb_ptr);

	/* Check for Grow */
	MemBufferCheckForGrow(mb_ptr, needed_size + 1);

	/* Calculate base MB_PTR, copy data IN and UPDATE size */
	base_ptr = ((char*)mb_ptr->data + new_data_offset);
	memcpy(base_ptr, new_data, new_data_sz);
	mb_ptr->size += needed_size;

	/* CRITICAL SECTION - END */
	if (mb_ptr->mb_type == BRBDATA_THREAD_SAFE)
		_MemBufferLeaveCritical(mb_ptr);

	/* Return number of size */
	return (mb_ptr->size);
}
/**************************************************************************************************************************/
unsigned long MemBufferAdd(MemBuffer *mb_ptr, void *new_data, unsigned long new_data_sz)
{
	void *base_ptr = NULL;

	/* Sanity checks */
	if (!mb_ptr)
		return 0;

	if (mb_ptr->flags.readonly)
		return 0;

	/* CRITICAL SECTION - BEGIN */
	if (mb_ptr->mb_type == BRBDATA_THREAD_SAFE)
		_MemBufferEnterCritical(mb_ptr);

	/* Check for Grow */
	MemBufferCheckForGrow(mb_ptr, new_data_sz + 1);

	/* Calculate base MemBuffer_ptr */
	base_ptr = ((char*)mb_ptr->data + mb_ptr->size);

	/* Add it to buffer */
	memcpy(base_ptr, new_data, new_data_sz);

	/* Update size */
	mb_ptr->size += new_data_sz;

	/* CRITICAL SECTION - END */
	if (mb_ptr->mb_type == BRBDATA_THREAD_SAFE)
		_MemBufferLeaveCritical(mb_ptr);

	/* Return number of size */
	return (mb_ptr->size);
}
/**************************************************************************************************************************/
MemBuffer *MemBufferMerge(MemBuffer *mb1_ptr, MemBuffer *mb2_ptr)
{
	MemBuffer *merged_mb;
	unsigned long mb1_sz = 0;
	unsigned long mb2_sz = 0;
	unsigned long new_mb_sz = 0;

	/* Sanity Check */
	if ( (!mb1_ptr) || (!mb2_ptr) )
		return 0;

	mb1_sz 		= MemBufferGetSize(mb1_ptr);
	mb2_sz 		= MemBufferGetSize(mb2_ptr);

	/* Calculated new mem buffer size */
	new_mb_sz 	= (mb1_sz + mb2_sz);

	/* Create new merged membuffer */
	merged_mb 	= MemBufferNew( BRBDATA_THREAD_SAFE, new_mb_sz);

	/* Add both first and second parts to merged */
	MemBufferAdd(merged_mb, MemBufferDeref(mb1_ptr), mb1_sz);
	MemBufferAdd(merged_mb, MemBufferDeref(mb2_ptr), mb2_sz);

	/* Return new MemBuffer */
	return merged_mb;
}
/**************************************************************************************************************************/
int MemBufferPrintf(MemBuffer *mb_ptr, char *message, ...)
{
	char fmt_buf[MEMBUFFER_MAX_PRINTF];
	va_list args;
	int msg_len;

	char *buf_ptr		= (char*)&fmt_buf;
	int msg_malloc		= 0;
	int alloc_sz		= MEMBUFFER_MAX_PRINTF;

	/* Sanity Check */
	if ((!mb_ptr) || (!message))
		return 0;

	/* Probe message size */
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

	/* Now actually print it and NULL terminate */
	msg_len = vsnprintf(buf_ptr, (alloc_sz - 1), message, args);
	buf_ptr[msg_len] = '\0';

	/* Finish with VA ARGs list */
	va_end(args);

	/* Add it to MB */
	MemBufferAdd(mb_ptr, buf_ptr, msg_len);

	/* Used MALLOC< release it */
	if (msg_malloc)
		free(buf_ptr);

	return 1;
}
/**************************************************************************************************************************/
int MemBufferSyncWriteToFile(MemBuffer *mb_ptr, char *filepath)
{
	void *data_ptr;
	unsigned long data_sz;
	int fd;

	int op_status = -1;

	/* Sanity check */
	if ( (!mb_ptr) || (!filepath) )
		return 0;

	/* Set UMASK */
	umask(222);

	/* Try to open file */
	fd = open(filepath, (O_WRONLY | O_TRUNC | O_CREAT | O_SYNC), 0644);

	//	O_DIRECT may be used to minimize or eliminate the cache effects of	read-
	//	     ing and writing.  The system will attempt to avoid	caching	the data you
	//	     read or write.  If	it cannot avoid	caching	the data, it will minimize the
	//	     impact the	data has on the	cache.	Use of this flag can drastically
	//	     reduce performance	if not used with care.

	/* Failed opening file */
	if (fd < 0)
		return 0;

	/* Grab data and WRITE */
	data_ptr	= MemBufferDeref(mb_ptr);
	data_sz		= MemBufferGetSize(mb_ptr);
	op_status	= write(fd, data_ptr, data_sz);

	/* Failed writing */
	if (op_status < 0)
	{
		close(fd);
		return 0;
	}

	/* Sync and close */
	fsync(fd);
	close(fd);

	return 1;
}
/**************************************************************************************************************************/
int MemBufferWriteToFile(MemBuffer *mb_ptr, char *filepath)
{
	unsigned long data_sz;
	void *data_ptr;
	int op_status;
	int fd;

	/* Sanity check */
	if ( (!mb_ptr) || (!filepath) )
		return 0;

	/* Set UMASK */
	umask(222);

	/* Try to open file */
	fd = open(filepath, (O_WRONLY | O_TRUNC | O_CREAT ), 0644);

	/* Failed opening file */
	if (fd < 0)
		return 0;

	/* Grab data and WRITE */
	data_ptr	= MemBufferDeref(mb_ptr);
	data_sz		= MemBufferGetSize(mb_ptr);
	op_status	= write(fd, data_ptr, data_sz);

	/* Failed writing */
	if (op_status < 0)
	{
		//printf("MemBufferWriteToFile - Write failed with OP_STATUS [%d] - ERRNO [%d] - Trying to write [%lu] BYTES\n", op_status, errno, data_sz);

		close(fd);
		return 0;
	}

	/* Close FD */
	close(fd);
	return 1;
}
/**************************************************************************************************************************/
int MemBufferPWriteToFD(MemBuffer *mb_ptr, unsigned long mb_offset, unsigned long file_offset, int file_fd)
{
	unsigned long data_sz;
	char *data_ptr;
	char *base_ptr;
	int op_status;

	/* Sanity check */
	if ((!mb_ptr) || (file_fd < 0))
		return 0;

	/* Set UMASK */
	umask(222);

	/* Grab data and WRITE */
	data_ptr	= MemBufferDeref(mb_ptr);
	data_sz		= MemBufferGetSize(mb_ptr);

	/* Will trespass boundary limit, stop */
	if (mb_offset > data_sz)
		return 0;

	/* Issue PWRITE */
	base_ptr	= (data_ptr + mb_offset);
	op_status	= pwrite(file_fd, base_ptr, (data_sz - mb_offset), file_offset);

	/* Failed writing */
	if (op_status < 0)
	{
		//printf("MemBufferPWriteToFD - PWrite failed with OP_STATUS [%d] - ERRNO [%d] - Trying to write [%lu] BYTES\n", op_status, errno, data_sz);
		return 0;
	}

	return op_status;
}
/**************************************************************************************************************************/
int MemBufferPWriteToFile(MemBuffer *mb_ptr, unsigned long mb_offset, unsigned long file_offset, char *filepath)
{
	unsigned long data_sz;
	char *data_ptr;
	char *base_ptr;
	int op_status;
	int fd;

	/* Sanity check */
	if ( (!mb_ptr) || (!filepath) )
		return 0;

	/* Set UMASK */
	umask(222);

	/* Try to open file */
	fd = open(filepath, (O_WRONLY | O_CREAT ), 0644);

	/* Failed opening file */
	if (fd < 0)
		return 0;

	/* Grab data and WRITE */
	data_ptr	= MemBufferDeref(mb_ptr);
	data_sz		= MemBufferGetSize(mb_ptr);

	/* Will trespass boundary limit, stop */
	if (mb_offset > data_sz)
	{
		close(fd);
		return 0;
	}

	/* Issue PWRITE */
	base_ptr	= (data_ptr + mb_offset);
	op_status	= pwrite(fd, base_ptr, (data_sz - mb_offset), file_offset);

	/* Failed writing */
	if (op_status < 0)
	{
		//printf("MemBufferPWriteToFile - PWrite failed with OP_STATUS [%d] - ERRNO [%d] - Trying to write [%lu] BYTES\n", op_status, errno, data_sz);

		close(fd);
		return 0;
	}

	/* Close FD */
	close(fd);
	return op_status;
}
/**************************************************************************************************************************/
int MemBufferOffsetWriteToFile(MemBuffer *mb_ptr, unsigned long offset, char *filepath)
{
	unsigned long data_sz;
	int fd;
	char *data_ptr;
	char *base_ptr;

	int op_status = -1;

	/* Sanity check */
	if ( (!mb_ptr) || (!filepath) )
		return 0;

	/* Set UMASK */
	umask(222);

	/* Try to open file */
	fd = open(filepath, (O_WRONLY | O_TRUNC | O_CREAT ), 0644);

	/* Failed opening file */
	if (fd < 0)
		return 0;

	data_ptr	= MemBufferDeref(mb_ptr);
	data_sz		= MemBufferGetSize(mb_ptr);

	if (offset > data_sz)
	{
		close(fd);
		return 0;
	}

	base_ptr	= (data_ptr + offset);
	op_status	= write(fd, base_ptr, (data_sz - offset));

	if (op_status < 0)
	{
		close(fd);
		return 0;
	}

	close(fd);

	return 1;
}
/**************************************************************************************************************************/
int MemBufferMmapWriteToFile(MemBuffer *mb_ptr, char *filepath)
{
	void *data_ptr;
	unsigned long data_sz;
	int fd;

	long op_status = -1;

	/* Sanity check */
	if ( (!mb_ptr) || (!filepath) )
	{
		return EINVAL;
	}

	/* Set UMASK and remove old file */
	umask(222);
	unlink(filepath);

	/* Try to open file */
	//	fd = open(filepath, (O_WRONLY | O_TRUNC | O_CREAT | O_SYNC), 0644);
	fd = open(filepath, O_RDWR | O_CREAT, 0644);

	//	O_DIRECT may be used to minimize or eliminate the cache effects of	read-
	//	     ing and writing.  The system will attempt to avoid	caching	the data you
	//	     read or write.  If	it cannot avoid	caching	the data, it will minimize the
	//	     impact the	data has on the	cache.	Use of this flag can drastically
	//	     reduce performance	if not used with care.

	/* Failed opening file */
	if (fd < 0)
	{
		return EBADF;
	}

	/* Grab data and WRITE */
	data_ptr	= MemBufferDeref(mb_ptr);
	data_sz		= MemBufferGetSize(mb_ptr);

	op_status	= lseek(fd, data_sz - 1, SEEK_SET);

	/* Failed lseek */
	if (op_status < 0)
	{
		close(fd);
		return ESPIPE;
	}

	op_status	= write(fd, "", 1);

	/* Failed writing */
	if (op_status < 0)
	{
		close(fd);
		return EIO;
	}

	void *map_ptr;

	map_ptr 	= mmap(0, data_sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	if (!map_ptr)
	{
		close(fd);
		return EPERM;
	}

	memcpy(map_ptr, data_ptr, data_sz);

	op_status	= msync(map_ptr, data_sz, MS_SYNC);

	/* Failed MemMap Sync */
	if (op_status < 0)
	{
		close(fd);
		return ENXIO;
	}

	op_status	= munmap(map_ptr, data_sz);

	/* Failed MemMap unmount */
	if (op_status < 0)
	{
		close(fd);
		return ENOEXEC;
	}

	/* Sync and close */
	//	fsync(fd);
	close(fd);

	return 0;
}
/**************************************************************************************************************************/
MemBuffer *MemBufferMmapFile(char *filepath)
{
	MemBuffer *mb_ptr;
	void *mmap_ptr;
	struct stat file_stat;
	unsigned long file_sz;
	int op_status;
	int fd;

	long bytes_read = -1;

	/* Sanity check */
	if (!filepath)
		return NULL;

	/* Try to open file */
	umask(S_IWGRP | S_IWOTH);

	fd = open(filepath, O_RDONLY);

	/* Failed opening file */
	if (fd < 0)
		return NULL;

	/* Acquire file status information */
	op_status = fstat(fd, &file_stat);

	if (op_status < 0)
	{
		close(fd);
		return NULL;
	}

	/* Get file size */
	file_sz = file_stat.st_size;

	/* MMAP file */
#if defined(__linux__) && defined(MAP_POPULATE)
	mmap_ptr = mmap(NULL, file_sz, PROT_READ | PROT_WRITE, MAP_POPULATE, fd, 0);
#else
	mmap_ptr = mmap(NULL, file_sz, PROT_READ | PROT_WRITE, MAP_PREFAULT_READ, fd, 0);
#endif
	if (MAP_FAILED  == mmap_ptr)
	{
		close(fd);
		return NULL;
	}

	BRB_CALLOC(mb_ptr, 1, sizeof(MemBuffer));

	/* Fill in data */
	mb_ptr->mb_type			= BRBDATA_THREAD_UNSAFE;
	mb_ptr->size			= file_sz;
	mb_ptr->capacity		= file_sz;

	/* Set flags */
	mb_ptr->flags.mmaped	= 1;
	mb_ptr->flags.readonly	= 1;

	/* Map file */
	mb_ptr->data			= mmap_ptr;
	mb_ptr->mmap_fd			= fd;

	return mb_ptr;
}
/**************************************************************************************************************************/
MemBuffer *MemBufferReadOnlyMmapFile(char *filepath)
{
	MemBuffer *mb_ptr;
	void *mmap_ptr;
	struct stat file_stat;
	unsigned long file_sz;
	int op_status;
	int fd;

	long bytes_read = -1;

	/* Sanity check */
	if (!filepath)
		return NULL;

	/* Try to open file */
	umask(S_IWGRP | S_IWOTH);

	fd = open(filepath, O_RDONLY);

	/* Failed opening file */
	if (fd < 0)
		return NULL;

	/* Acquire file status information */
	op_status = fstat(fd, &file_stat);

	if (op_status < 0)
	{
		close(fd);
		return NULL;
	}

	/* Get file size */
	file_sz = file_stat.st_size;

	/* MMAP file */
#if defined(__linux__) && defined(MAP_POPULATE)
	mmap_ptr = mmap(NULL, file_sz, (PROT_READ), MAP_POPULATE, fd, 0);
#else
	mmap_ptr = mmap(NULL, file_sz, (PROT_READ), MAP_PREFAULT_READ, fd, 0);
#endif
	if (MAP_FAILED  == mmap_ptr)
	{
		close(fd);
		return NULL;
	}

	BRB_CALLOC(mb_ptr, 1, sizeof(MemBuffer));

	/* Fill in data */
	mb_ptr->mb_type			= BRBDATA_THREAD_UNSAFE;
	mb_ptr->size			= file_sz;
	mb_ptr->capacity		= file_sz;

	/* Set flags */
	mb_ptr->flags.mmaped	= 1;
	mb_ptr->flags.readonly	= 1;

	/* Map file */
	mb_ptr->data			= mmap_ptr;
	mb_ptr->mmap_fd			= fd;

	return mb_ptr;
}
/**************************************************************************************************************************/
MemBuffer *MemBufferReadFromFile(char *filepath)
{
	MemBuffer *mb_ptr;
	struct stat file_stat;
	char *read_buffer;
	unsigned long file_sz;
	int op_status;
	int fd;

	long bytes_read = -1;

	/* Sanity check */
	if (!filepath)
		return NULL;

	/* Check if table exists already */
	op_status = access(filepath, R_OK);

	/* Nothing to read from, bail out */
	if (op_status < 0)
		return NULL;

	/* Try to open file */
	//umask(S_IWGRP | S_IWOTH);

	fd = open(filepath, O_RDONLY);

	/* Failed opening file */
	if (fd < 0)
		return NULL;

	/* Acquire file status information */
	fstat(fd, &file_stat);

	/* Get file size */
	file_sz = file_stat.st_size;

	/* Alloc space for raw file content */
	BRB_CALLOC(read_buffer,  (file_sz + 1), sizeof(char) );

	/* Read the buffer from the file descritor, and then close it down */
	bytes_read = read(fd, read_buffer, file_sz);

	/* Error reading */
	if (bytes_read < 0)
	{
		/* Free resources and return */
		BRB_FREE(read_buffer);
		close(fd);
		return NULL;
	}

	/* Partial read error */
	if (bytes_read != file_sz)
	{
		/* Free resources and return */
		BRB_FREE(read_buffer);
		close(fd);
		return NULL;

	}

	/* Create a new mem_buf, using file size as grow rate */
	mb_ptr = MemBufferNew(BRBDATA_THREAD_UNSAFE, file_sz);

	/* Add data to mem_buf */
	MemBufferAdd(mb_ptr, read_buffer, bytes_read);

	/* Free resources and return */
	close(fd);
	BRB_FREE(read_buffer);

	return mb_ptr;
}
/**************************************************************************************************************************/
MemBuffer *MemBufferReadFromBigFile(char *filepath)
{
	MemBuffer *mb_ptr;
	struct stat file_stat;
	char *read_buffer;
	unsigned long file_sz;
	int op_status;
	int fd;

	long bytes_read = -1;
	unsigned long bytes_offset = 0;
	unsigned long bytes_block = (1048576 * 2);

	/* Sanity check */
	if (!filepath)
		return NULL;

	/* Check if table exists already */
	op_status = access(filepath, R_OK);

	/* Nothing to read from, bail out */
	if (op_status < 0)
		return NULL;

	/* Try to open file */
	//umask(S_IWGRP | S_IWOTH);

	fd = open(filepath, O_RDONLY);

	/* Failed opening file */
	if (fd < 0)
		return NULL;

	/* Acquire file status information */
	fstat(fd, &file_stat);

	/* Get file size */
	file_sz = file_stat.st_size;

	/* Alloc space for raw file content */
	BRB_CALLOC(read_buffer, bytes_block + 1, sizeof(char));

	/* Create a new mem_buf, using file size as grow rate */
	mb_ptr 			= MemBufferNew(BRBDATA_THREAD_UNSAFE, file_sz);

	while (bytes_offset < file_sz)
	{
		bytes_read 	= read(fd, read_buffer, bytes_block);

		//		printf("bytes_offset [%lu] [%lu]\n", bytes_offset, file_sz);

		if (bytes_read <= 0)
		{
			//			printf("bytes_read fail [%d] [%ld]\n", errno, bytes_read);

			/* Free resources and return */
			BRB_FREE(read_buffer);
			close(fd);

			MemBufferDestroy(mb_ptr);

			return NULL;
		}

		bytes_offset = (bytes_offset + bytes_read);

		/* Add data to mem_buf */
		MemBufferAdd(mb_ptr, read_buffer, bytes_read);
	}

	/* Free resources and return */
	BRB_FREE(read_buffer);
	close(fd);

	return mb_ptr;
}
/**************************************************************************************************************************/
long MemBufferReadFromFileOffseted(MemBuffer *mb_ptr, unsigned long file_offset, long data_sz, char *filepath)
{
	struct stat file_stat;
	unsigned long file_sz;
	int op_status;
	int fd;

	long bytes_read = -1;

	/* Sanity check */
	if (!filepath)
		return -1;

	/* Check if table exists already */
	op_status 	= access(filepath, R_OK);

	/* Nothing to read from, bail out */
	if (op_status < 0)
		return -2;

	fd 			= open(filepath, O_RDONLY);

	/* Failed opening file */
	if (fd < 0)
		return -3;

	/* Acquire file status information */
	fstat(fd, &file_stat);

	/* Get file size */
	file_sz 	= file_stat.st_size;

	if (data_sz < 0)
		data_sz = (file_sz - file_offset);

	/* Can't read, partial data is great than file size */
	if ((data_sz + file_offset) > file_sz)
	{
		/* Free resources and return */
		close(fd);

		return -4;
	}

	/* Read partial data from file */
	bytes_read 	= MemBufferPReadFromFD(mb_ptr, file_offset, data_sz, fd);

	/* Free resources and return */
	close(fd);

	return bytes_read;
}
/**************************************************************************************************************************/
long MemBufferPReadFromFD(MemBuffer *mb_ptr, unsigned long file_offset, unsigned long data_sz, int fd)
{
	long bytes_read = -1;
	char *read_buffer;

	/* Sanity check */
	if ((!mb_ptr) || (fd < 0))
		return 0;

	/* Set UMASK */
	umask(222);

	/* Sanity check */
	if (!mb_ptr)
		return 0;

	/* Alloc space for raw file content */
	BRB_CALLOC(read_buffer,  (data_sz + 1), sizeof(char) );

	/* Read the buffer from the file descriptor, and then close it down */
	bytes_read = pread(fd, read_buffer, data_sz, file_offset);

	/* Error reading */
	if (bytes_read < 0)
	{
		/* Free resources and return */
		BRB_FREE(read_buffer);
		return 0;
	}

	/* Add data to mem_buf */
	MemBufferAdd(mb_ptr, read_buffer, bytes_read);

	BRB_FREE(read_buffer);
	return bytes_read;
}
/**************************************************************************************************************************/
long MemBufferReadFromFD(MemBuffer *mb_ptr, unsigned long data_sz, int fd)
{
	long bytes_read = -1;
	char *read_buffer;

	/* Sanity check */
	if (!mb_ptr)
		return 0;

	/* Alloc space for raw file content */
	BRB_CALLOC(read_buffer,  (data_sz + 1), sizeof(char) );

	/* Read the buffer from the file descriptor, and then close it down */
	bytes_read = read(fd, read_buffer, data_sz);

	/* Error reading */
	if (bytes_read < 0)
	{
		/* Free resources and return */
		BRB_FREE(read_buffer);
		return 0;
	}

	/* Add data to mem_buf */
	MemBufferAdd(mb_ptr, read_buffer, bytes_read);

	BRB_FREE(read_buffer);

	return bytes_read;
}
/**************************************************************************************************************************/
long MemBufferAppendFromFD(MemBuffer *mb_ptr, unsigned long data_sz, int fd, int flags)
{
	long bytes_read = -1;
	char *read_buffer;

	/* Sanity check */
	if (!mb_ptr)
		return 0;

	/* Alloc space for raw file content */
	BRB_CALLOC(read_buffer,  (data_sz + 1), sizeof(char) );

	/* Read the buffer from the file descriptor, and then close it down */
	bytes_read = recv(fd, read_buffer, data_sz, flags);

	/* Error reading */
	if (bytes_read <= 0)
	{
		/* Free resources and return */
		BRB_FREE(read_buffer);
		return bytes_read;
	}

	/* Add data to mem_buf */
	MemBufferAdd(mb_ptr, read_buffer, bytes_read);

	BRB_FREE(read_buffer);

	return bytes_read;

}
/**************************************************************************************************************************/
long MemBufferAppendToFD(MemBuffer *mb_ptr, int fd)
{
	long bytes_write = -1;

	/* Sanity check */
	if (!mb_ptr || fd < 1)
		return 0;

	/* Write MB to FD */
	bytes_write = write(fd, MemBufferDeref(mb_ptr), MemBufferGetSize(mb_ptr));

	/* Error writing */
	if (bytes_write < 0)
		return 0;

	return bytes_write;
}
/**************************************************************************************************************************/
int MemBufferAppendFromFile(MemBuffer *mb_ptr, char *filepath)
{
	struct stat file_stat;
	int fd;
	int file_sz;

	int bytes_read = -1;

	char *read_buffer;

	/* Sanity check */
	if ((!mb_ptr) || (!filepath))
		return 0;

	/* Try to open file */
	umask(S_IWGRP | S_IWOTH);

	fd = open(filepath, O_RDONLY);

	/* Failed opening file */
	if (fd < 0)
		return 0;

	/* Aquire file status information */
	fstat(fd, &file_stat);

	/* Get file size */
	file_sz = file_stat.st_size;

	/* Alloc space for raw file content */
	BRB_CALLOC(read_buffer,  (file_sz + 1), sizeof(char) );

	/* Read the buffer from the file descritor, and then close it down */
	bytes_read = read(fd, read_buffer, file_sz);

	/* Error reading */
	if (bytes_read < 0)
	{
		/* Free resources and return */
		BRB_FREE(read_buffer);
		close(fd);
		return 0;
	}

	/* Partial read error */
	if (bytes_read != file_sz)
	{
		/* Free resources and return */
		BRB_FREE(read_buffer);
		close(fd);
		return 0;

	}

	/* Add data to mem_buf */
	MemBufferAdd(mb_ptr, read_buffer, bytes_read);

	/* Free resources and return */
	close(fd);
	BRB_FREE(read_buffer);

	return 1;



}
/**************************************************************************************************************************/
int MemBufferShow(MemBuffer *mb_ptr, FILE *fd)
{
	unsigned long data_sz;
	char *ptr;
	int i;

	/* CRITICAL SECTION - BEGIN */
	if (mb_ptr->mb_type == BRBDATA_THREAD_SAFE)
		_MemBufferEnterCritical(mb_ptr);

	fprintf(fd, "------------------------------------------\n");
	fprintf(fd, "mem buf size     -> [%lu]\n", mb_ptr->size);
	fprintf(fd, "mem buf capacity -> [%lu]\n", mb_ptr->capacity);
	fprintf(fd, "------------------------------------------\n");
	fprintf(fd, "data -> [%s]\n", (char *)mb_ptr->data);
	fprintf(fd, "------------------------------------------\n");

	/* CRITICAL SECTION - END */
	if (mb_ptr->mb_type == BRBDATA_THREAD_SAFE)
		_MemBufferLeaveCritical(mb_ptr);

	return 1;
}
/**************************************************************************************************************************/
void *MemBufferDeref(MemBuffer *mb_ptr)
{
	char *deref_ptr;

	/* Sanity check */
	if (!mb_ptr)
		return NULL;

	deref_ptr	= mb_ptr->data;
	deref_ptr	+= mb_ptr->offset;

	return deref_ptr;
}
/**************************************************************************************************************************/
void *MemBufferOffsetDeref(MemBuffer *mb_ptr, unsigned long offset)
{
	char *deref_ptr;

	/* Sanity check */
	if (!mb_ptr)
		return NULL;

	/* Offset exceeds buffer size */
	if (offset > MemBufferGetCapacity(mb_ptr))
		return NULL;

	deref_ptr	= MemBufferDeref(mb_ptr);
	deref_ptr	+= offset;

	return deref_ptr;
}
/**************************************************************************************************************************/
unsigned long MemBufferOffsetGet(MemBuffer *mb_ptr)
{
	/* Sanity check */
	if (!mb_ptr)
		return 0;

	return mb_ptr->offset;
}
/**************************************************************************************************************************/
unsigned long MemBufferOffsetSet(MemBuffer *mb_ptr, unsigned long offset)
{
	/* Sanity check */
	if (!mb_ptr)
		return 0;

	mb_ptr->offset = offset;
	return mb_ptr->offset;
}
/**************************************************************************************************************************/
unsigned long MemBufferOffsetReset(MemBuffer *mb_ptr)
{
	/* Sanity check */
	if (!mb_ptr)
		return 0;

	mb_ptr->offset = 0;

	return 1;
}
/**************************************************************************************************************************/
unsigned long MemBufferOffsetIncrement(MemBuffer *mb_ptr, unsigned long offset)
{
	/* Sanity check */
	if (!mb_ptr)
		return 0;

	mb_ptr->offset += offset;
	return mb_ptr->offset;
}
/**************************************************************************************************************************/
int MemBufferOffsetWrite(MemBuffer *mb_ptr, unsigned long offset, void *data, unsigned long data_sz)
{
	void *data_ptr;
	unsigned long write_end;

	/* Sanity check */
	if ((!mb_ptr) || (offset < 0))
		return 0;

	/* Get current size and offset where write will finish */
	write_end	= (offset + data_sz);

	/* Check if we need to grow our buffer */
	MemBufferCheckForGrow(mb_ptr, (write_end + 1));

	/* Get buffer PTR with offset */
	data_ptr	= MemBufferOffsetDeref(mb_ptr, offset);

	/* Copy data */
	memcpy(data_ptr, data, data_sz);

	/* Update size */
	mb_ptr->size = (write_end < mb_ptr->size ? mb_ptr->size : write_end);

	return 1;
}
/**************************************************************************************************************************/
int _MemBufferEnterCritical(MemBuffer *mb_ptr)
{
	/* Sanity Check */
	if (!mb_ptr)
		return MEMBUFFER_ACQUIRE_FAIL;

	/* Lock the mutex */
	pthread_mutex_lock(&mb_ptr->mutex);

	/* Acquire dynarray */
	EBIT_CLR(mb_ptr->busy_flags, MEMBUFFER_FREE);
	EBIT_SET(mb_ptr->busy_flags, MEMBUFFER_BUSY);

	return MEMBUFFER_ACQUIRE_SUCCESS;
}
/**************************************************************************************************************************/
int _MemBufferLeaveCritical(MemBuffer *mb_ptr)
{
	/* Sanity Check */
	if (!mb_ptr)
		return MEMBUFFER_ACQUIRE_FAIL;

	/* Lock the mutex */
	pthread_mutex_unlock(&mb_ptr->mutex);

	/* Release dynarray */
	EBIT_CLR(mb_ptr->busy_flags, MEMBUFFER_BUSY);
	EBIT_SET(mb_ptr->busy_flags, MEMBUFFER_FREE);

	return MEMBUFFER_ACQUIRE_SUCCESS;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
void MemBufferEncryptData(MemBuffer *mb_ptr, unsigned int seed, unsigned long offset)
{
	BRB_BLOWFISH_CTX _blow_context;

	unsigned long data_size = (MemBufferGetSize(mb_ptr) + offset);
	unsigned long i, blocks = (data_size / sizeof(long));
	unsigned long *raw_data_ptr;

	/* Create encryption key */
	unsigned int enc_key[16];

	/* Initialize the encryption key */
	for (i = 0; i < 16; i++)
	{
		enc_key[i] = ( ((i+seed) * seed) + (13 * i) );
		seed = enc_key[i] * seed;
	}

	/* Padding */
	blocks += 2;

	/* CRITICAL SECTION - BEGIN */
	if (mb_ptr->mb_type == BRBDATA_THREAD_SAFE)
		_MemBufferEnterCritical(mb_ptr);

	/* Check for Grow */
	MemBufferCheckForGrow(mb_ptr, ((blocks+1) * sizeof(long)) );

	/* Create Encryption/Decryption context */
	BRB_Blowfish_Init(&_blow_context, (unsigned char*)enc_key, sizeof(enc_key[16]) );

	if (offset)
		raw_data_ptr = (unsigned long*)MemBufferOffsetDeref(mb_ptr, offset);
	else
		raw_data_ptr = (unsigned long*)MemBufferDeref(mb_ptr);

	//	printf("MemBufferEncryptData - Encrypting file (%d bytes) - %d blocos\n", data_size, blocks);

	/* Encrypt the package */
	for(i = 0; i < blocks; i+=2)
		BRB_Blowfish_Encrypt(&_blow_context, (unsigned long*)&raw_data_ptr[i], (unsigned long*)&raw_data_ptr[i+1]);

	/* Update MemBuffsize */
	mb_ptr->size = ((i * sizeof(long)) + offset);

	//	printf("MemBufferEncryptData - Adjusting size to (%d bytes) - %d blocos\n", data_size, blocks);

	/* CRITICAL SECTION - END */
	if (mb_ptr->mb_type == BRBDATA_THREAD_SAFE)
		_MemBufferLeaveCritical(mb_ptr);

	return;
}
/**************************************************************************************************************************/
void MemBufferDecryptData(MemBuffer *mb_ptr, unsigned int seed, unsigned long offset)
{
	BRB_BLOWFISH_CTX _blow_context;

	unsigned long data_size = (MemBufferGetSize(mb_ptr) - offset);
	unsigned long i, blocks = (data_size / sizeof(long));
	unsigned long *raw_data_ptr;

	/* Create encryption key */
	unsigned int enc_key[16];

	/* Initialize the encryption key */
	for (i = 0; i < 16; i++)
	{
		enc_key[i] = ( ((i+seed) * seed) + (13 * i) );
		seed = enc_key[i] * seed;
	}

	/* Remove Padding */
	blocks += 2;

	/* CRITICAL SECTION - BEGIN */
	if (mb_ptr->mb_type == BRBDATA_THREAD_SAFE)
		_MemBufferEnterCritical(mb_ptr);

	/* Check for Grow */
	MemBufferCheckForGrow(mb_ptr, ((blocks+1)*sizeof(long)));

	/* Create Encryption/Decryption context */
	BRB_Blowfish_Init(&_blow_context, (unsigned char*)enc_key, sizeof(enc_key) );

	if (offset)
		raw_data_ptr = (unsigned long*)MemBufferOffsetDeref(mb_ptr, offset);
	else
		raw_data_ptr = (unsigned long*)MemBufferDeref(mb_ptr);

	//	printf("MemBufferDecryptData - Decryptando pacote (%d bytes) - %d blocos\n", (blocks*sizeof(long)), blocks);

	int removePadding = 0;
	for(i = 0; i < blocks; i+=2)
	{
		/* Remove padding gremlin */
		if ( (raw_data_ptr[i] == 0) || ( raw_data_ptr[i+1] == 0) )
			break;

		BRB_Blowfish_Decrypt(&_blow_context, (unsigned long*)&raw_data_ptr[i], (unsigned long*)&raw_data_ptr[i+1]);
	}

	//	if (removePadding)
	//		printf("MemBufferDecryptData - Remove padding (%d bytes)\n", removePadding);
	//	else
	//		printf("MemBufferDecryptData - No padding to remove\n", removePadding);

	//	printf("MemBufferDecryptData - Updating size (%d bytes) - %d blocos\n", (((i)*sizeof(long))+offset), blocks);

	/* Update MemBuffsize */
	mb_ptr->size = ((((i)*sizeof(long))+offset)-removePadding);

	/* CRITICAL SECTION - END */
	if (mb_ptr->mb_type == BRBDATA_THREAD_SAFE)
		_MemBufferLeaveCritical(mb_ptr);

	return;

}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
int MemBufferNoCoreOnCrash(MemBuffer *mb_ptr)
{
	int advise_status;

	/* Sanity checks */
	if (!mb_ptr)
		return 0;

	/* Mark as NO_CORE */
	mb_ptr->flags.no_core = 1;

	/* Tells the kernel we DO NOT want this buffer as part of a COREDUMP on CRASH */
	if (mb_ptr->capacity > 0)
	{
#if defined(__linux__) && defined(MADV_DONTDUMP)
		advise_status = madvise(mb_ptr->data, mb_ptr->capacity, MADV_DONTDUMP);
#else
		advise_status = madvise(mb_ptr->data, mb_ptr->capacity, MADV_NOCORE);
#endif
		return (0 == advise_status) ? 1 : 0;
	}

	return 1;
}
/**************************************************************************************************************************/
int MemBufferUnwirePages(MemBuffer *mb_ptr)
{
	int lock_status;

	/* Sanity checks */
	if (!mb_ptr)
		return 0;

	/* Not wired, bail out */
	if (!mb_ptr->flags.wired)
		return 0;

	lock_status = munlock(mb_ptr->data, mb_ptr->capacity);
	mb_ptr->flags.wired = 0;

	return 1;
}
/**************************************************************************************************************************/
int MemBufferWirePages(MemBuffer *mb_ptr)
{
	int lock_status;

	/* Sanity checks */
	if (!mb_ptr)
		return 0;

	/* Already wired, bail out */
	if (mb_ptr->flags.wired)
		return 1;

	/* Mark WIRED to wire once we create buffer */
	if (mb_ptr->capacity == 0)
	{
		mb_ptr->flags.wired = 1;
		return 1;
	}

	lock_status = mlock(mb_ptr->data, mb_ptr->capacity);
	mb_ptr->flags.wired = (0 == lock_status) ? 1 : 0;

	return mb_ptr->flags.wired;
}
/**************************************************************************************************************************/
int MemBufferMetaDataPack(MemBuffer *mb_ptr, MetaData *dst_metadata, unsigned long item_sub_id)
{
	/* Sanity check */
	if (!mb_ptr)
		return 0;

	/* Clean up stack */
	memset(&mb_ptr->metadata, 0, sizeof(MemBufferMetaData));

	/* Populate MB_META */
	mb_ptr->metadata.data_state		= mb_ptr->data_state;
	mb_ptr->metadata.data_type		= mb_ptr->data_type;
	mb_ptr->metadata.grow_rate		= mb_ptr->grow_rate;
	mb_ptr->metadata.offset			= mb_ptr->offset;
	mb_ptr->metadata.size			= MemBufferGetSize(mb_ptr);
	mb_ptr->metadata.user_int		= mb_ptr->user_int;
	mb_ptr->metadata.user_long		= mb_ptr->user_long;

	//printf("MemBufferMetaDataPack - GROW [%ld] - SIZE [%ld] - OFFSET [%ld]\n", mb_ptr->metadata.grow_rate, mb_ptr->metadata.size, mb_ptr->metadata.offset);

	/* Dump data to METADATA */
	MetaDataItemAdd(dst_metadata, DATATYPE_MEM_BUFFER_META, item_sub_id, &mb_ptr->metadata, sizeof(MemBufferMetaData));
	MetaDataItemAdd(dst_metadata, DATATYPE_MEM_BUFFER_DATA, item_sub_id, MemBufferDeref(mb_ptr), MemBufferGetSize(mb_ptr));

	return 1;
}
/**************************************************************************************************************************/
int MemBufferMetaDataUnPack(MemBuffer *mb_ptr, MemBuffer *raw_metadata_mb)
{
	MemBufferMetaData *mem_buf_meta;
	MetaDataItem *meta_item;
	MetaData metadata;
	int op_status;
	int i;

	/* Clean up stack and unpack METADATA */
	memset(&metadata, 0, sizeof(MetaData));
	op_status = MetaDataUnpack(&metadata, raw_metadata_mb, NULL);

	/* Failed unpacking */
	if (METADATA_UNPACK_SUCCESS != op_status)
	{
		/* Clean UP METDATA and leave */
		MetaDataClean(&metadata);
		return (-op_status);
	}

	/* Now walk all items */
	for (i = metadata.item_offset; i < metadata.items.count; i++)
	{
		meta_item = MemArenaGrabByID(metadata.items.arena, i);

		/* MEMBUFFER METADATA, populate back */
		if (DATATYPE_MEM_BUFFER_META == meta_item->item_id)
		{
			mem_buf_meta = meta_item->ptr;

			/* Populate MB_META - Do not touch SIZE, as it will mess MemBufferAdd we will dispatch once we see data */
			mb_ptr->data_state 	= mem_buf_meta->data_state;
			mb_ptr->data_type 	= mem_buf_meta->data_type;
			mb_ptr->grow_rate 	= mem_buf_meta->grow_rate;
			mb_ptr->offset 		= mem_buf_meta->offset;
			mb_ptr->user_int	= mem_buf_meta->user_int;
			mb_ptr->user_long	= mem_buf_meta->user_long;

			//printf("MemBufferMetaDataUnPack - DATATYPE_MEM_BUFFER_META - GROW [%ld] - SIZE [%ld] - OFFSET [%ld]\n", mem_buf_meta->grow_rate, mem_buf_meta->size, mem_buf_meta->offset);

			continue;
		}
		/* MEMBUFFER RAW_DATA, copy */
		else if (DATATYPE_MEM_BUFFER_DATA == meta_item->item_id)
		{
			//printf("MemBufferMetaDataUnPack - DATATYPE_MEM_BUFFER_DATA - [%ld] bytes\n", meta_item->sz);
			MemBufferAdd(mb_ptr, meta_item->ptr, meta_item->sz);
			continue;
		}

		continue;
	}

	/* Clean UP METDATA and leave */
	MetaDataClean(&metadata);
	return 1;
}
/**************************************************************************************************************************/
MemBuffer *MemBufferMetaDataPackToMB(MemBuffer *mb_ptr, unsigned long item_sub_id)
{
	MetaData metadata;
	MemBuffer *packed_mb = MemBufferNew(BRBDATA_THREAD_UNSAFE, 1024);

	/* Clean up stack */
	memset(&metadata, 0, sizeof(MetaData));

	/* Pack data into METADATA SERIALIZER and DUMP it into MEMBUFFER */
	MemBufferMetaDataPack(mb_ptr, &metadata, item_sub_id);
	MetaDataPack(&metadata, packed_mb);

	/* Clean UP METDATA and leave */
	MetaDataClean(&metadata);

	return packed_mb;
}
/**************************************************************************************************************************/
MemBuffer *MemBufferMetaDataUnPackFromMB(MemBuffer *raw_metadata_mb)
{
	MemBuffer *mb_ptr;
	int op_status;

	/* Create a new MEM_BUFFER and unpack */
	mb_ptr		= MemBufferNew(BRBDATA_THREAD_UNSAFE, 512);
	op_status	= MemBufferMetaDataUnPack(mb_ptr, raw_metadata_mb);

	/* Failed unpacking METADATA, destroy and LEAVE */
	if (op_status <= 0)
	{
		//printf("MemBufferMetaDataUnPackFromMB - Failed unpacking MEM_BUFFER\n");
		MemBufferDestroy(mb_ptr);
		return NULL;
	}

	return mb_ptr;
}
/**************************************************************************************************************************/
#define windowBits			15
#define ENABLE_ZLIB_GZIP	32
#define GZIP_CHUNK_SZ		0x4000

/**************************************************************************************************************************/
int MemBufferGzipDeflate(MemBuffer *in_mb, int in_offset, MemBuffer *out_mb)
{
	z_stream strm;
	unsigned char out_buf_data[GZIP_CHUNK_SZ];
	unsigned char *out_buf_ptr;
	unsigned long out_buf_space;
	unsigned long out_buf_sz;
	int op_status;

	/* Clean up stack */
	memset(&strm, 0, sizeof(z_stream));

	/* Point to buffer in stack area */
	out_buf_ptr		= (unsigned char *)&out_buf_data;
	out_buf_sz		= GZIP_CHUNK_SZ;

	strm.next_in 	= (unsigned char *)MemBufferDeref(in_mb);
	strm.avail_in 	= MemBufferGetSize(in_mb);
	strm.total_in  	= strm.avail_in;
	strm.next_out  	= out_buf_ptr;
	strm.avail_out 	= out_buf_sz;
	strm.total_out 	= strm.avail_out;

	/* Initialize ZLIB context */
	op_status 		= deflateInit(&strm, Z_DEFAULT_COMPRESSION);
	//op_status 	= deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits + ENABLE_ZLIB_GZIP, 8, Z_DEFAULT_STRATEGY);

	/* Failed initializing ZLIP context */
	if (Z_OK != op_status)
		return 0;

	/* Begin deflating data */
	do {

		strm.avail_out	= out_buf_sz;
		strm.next_out 	= out_buf_ptr;

		op_status 		= deflate(&strm, Z_FINISH);

		if (op_status < 0)
			return 0;

		out_buf_space 	= out_buf_sz - strm.avail_out;
		MemBufferAdd(out_mb, out_buf_ptr, out_buf_space);
	}
	while (strm.avail_out == 0);

	/* Finish DEFLATE context */
	op_status = deflateEnd(&strm);

	/* Failed finishing context */
	if (Z_OK != op_status)
		return -1;

	return 1;
}
/**************************************************************************************************************************/
int MemBufferGzipInflate(MemBuffer *in_mb, int in_offset, MemBuffer *out_mb)
{
	z_stream strm;
	unsigned char out_buf_data[GZIP_CHUNK_SZ + 1];
	unsigned char *out_buf_ptr;
	unsigned long out_buf_space;
	unsigned long out_buf_sz;
	int op_status;

	/* Clean up stack */
	memset(&strm, 0, sizeof(z_stream));

	/* Point to buffer in stack area */
	out_buf_ptr		= (unsigned char *)&out_buf_data;
	out_buf_sz		= GZIP_CHUNK_SZ;

	/* Fill in ZLIB context data */
	strm.next_in 	= (unsigned char *)MemBufferOffsetDeref(in_mb, in_offset);
	strm.avail_in 	= MemBufferGetSize(in_mb);
	strm.total_in  	= strm.avail_in;
	strm.next_out  	= out_buf_ptr;
	strm.avail_out 	= out_buf_sz;
	strm.total_out 	= strm.avail_out;

	/* Initialize ZLIB context */
	op_status 		= inflateInit2(&strm, windowBits + ENABLE_ZLIB_GZIP);

	/* Failed initializing ZLIP context */
	if (Z_OK != op_status)
		return -1;

	/* Begin inflating data */
	do
	{
		strm.avail_out	= out_buf_sz;
		strm.next_out 	= out_buf_ptr;
		op_status 		= inflate(&strm, Z_NO_FLUSH);

		if (op_status < 0)
			return -1;

		out_buf_space = (out_buf_sz - strm.avail_out);
		MemBufferAdd(out_mb, out_buf_ptr, out_buf_space);
	}
	while (strm.avail_out == 0);

	/* Finish inflate context */
	op_status 			= inflateEnd(&strm);

	/* Failed finishing context */
	if (Z_OK != op_status)
		return -1;

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void MemBufferCheckForGrow(MemBuffer *mb_ptr, unsigned long new_data_sz)
{
	void *base_ptr = NULL;
	unsigned long need_mem = 0;

	/* Calculate needed memory */
	need_mem = (mb_ptr->grow_rate + new_data_sz);

	/* Check if its the first element. If true, alloc. Else realloc */
	if (mb_ptr->capacity == 0)
	{
		BRB_CALLOC(mb_ptr->data, need_mem, sizeof(char));

		/* Update capacity */
		mb_ptr->capacity += need_mem;

		/* First WIRE */
		if (mb_ptr->flags.wired)
		{
			/* Little hack for first WIRE */
			mb_ptr->flags.wired = 0;
			MemBufferWirePages(mb_ptr);
		}

		/* Remove from COREDUMPs */
		if (mb_ptr->flags.no_core)
			MemBufferNoCoreOnCrash(mb_ptr);

	}

	/* Time to grow up the buffer */
	else if ( mb_ptr->capacity <= (mb_ptr->size + new_data_sz))
	{
		BRB_REALLOC(mb_ptr->data, mb_ptr->data, ((mb_ptr->capacity + need_mem) * sizeof(char)));

		/* RESET WIRE_MEM if WIRED */
		if (mb_ptr->flags.wired)
		{
			/* Reset WIRE for pages */
			MemBufferUnwirePages(mb_ptr);
			MemBufferWirePages(mb_ptr);
		}

		/* Remove from COREDUMPs */
		if (mb_ptr->flags.no_core)
			MemBufferNoCoreOnCrash(mb_ptr);

		/* Calculate base MemBuffer_ptr */
		base_ptr = ((char*)mb_ptr->data + mb_ptr->capacity);

		/* Clean buffer received by realloc */
		memset( base_ptr, 0, (need_mem * sizeof(char)));

		/* Update capacity */
		mb_ptr->capacity += need_mem;

	}

	return;
}
/**************************************************************************************************************************/
static int MemBufferDoDestroy(MemBuffer *mb_ptr)
{
	/* Release WIRE, if WIRED */
	MemBufferUnwirePages(mb_ptr);

	/* Destroy MUTEX, if MT_SAFE */
	if (mb_ptr->mb_type == BRBDATA_THREAD_SAFE)
		pthread_mutex_destroy(&mb_ptr->mutex);

	if (mb_ptr->flags.mmaped)
	{
		munmap(mb_ptr->data, mb_ptr->capacity);
		close(mb_ptr->mmap_fd);
	}
	else
	{
		BRB_FREE(mb_ptr->data);
	}

	BRB_FREE(mb_ptr);

	return 1;
}
/**************************************************************************************************************************/

