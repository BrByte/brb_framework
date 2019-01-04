/*
 * libbrb_data.h
 *
 *  Created on: 2011-10-11
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

#ifndef LIBBRB_DATA_H_
#define LIBBRB_DATA_H_

#include <ctype.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <regex.h>
#include <unistd.h>
#include <semaphore.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/shm.h>
#include <sys/sem.h>

#include <net/if.h>
#include <net/if_arp.h>
#if !defined(__linux__)
#include <net/if_dl.h>
#include <net/bpf.h>
#else
#include <bsd/string.h>
#include <bsd/stdlib.h>
#include <netinet/ether.h>
#endif
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <termios.h>
#include <assert.h>
#include <math.h>

/* Linux portable code */
#define IS_LINUX 0

#define _POSIX_C_SOURCE 200809L
//#define _POSIX_C_SOURCE 199309L

#if IS_LINUX
#include <stdint.h>
#endif


#define BRB_MALLOC(ptr, size) 				ptr = malloc(count, size)
#define BRB_CALLOC(ptr, count, size) 		ptr = calloc(count, size)
#define BRB_REALLOC(ptr_dst, ptr_src, size) ptr_dst = realloc(ptr_src, size)
#define BRB_STRDUP(ptr_dst, ptr_src) 		ptr_dst = strdup(ptr_src)
#define BRB_FREE(ptr) 						if(ptr) free(ptr)
#define BRB_SKIP_WHITESPACES(str) while ((*str == 13) || (*str == 10) || (*str == 9) || (*str == 32) || (*str == 11)) str++;
#define BRB_SKIP_ZEROSTR(str) while (str && *str == '0') str++;
#define BRB_CSTRING_TOUPPER(str) {int _i_ = 0;  while (str[_i_] != '\0') {	if (str[_i_] >= 'a' && str[_i_] <= 'z')	 str[_i_] -= 32; _i_++;	} }
#define BRB_CSTRING_TOLOWER(str) {int _i_ = 0;  while (str[_i_] != '\0') {	if (str[_i_] >= 'A' && str[_i_] <= 'Z')	 str[_i_] += 32; _i_++;	} }
#define BRB_CSTRING_TOTOKEN(str, token, off) {int _i_ = 0;  while (str[_i_] != '\0') {	if (str[_i_] == token) { str += _i_ + off; break;  } _i_++; } }
#define BRB_TIMEVAL_DELTA (when, now) ((now->tv_sec - when->tv_sec) * 1000 + (now->tv_usec - when->tv_usec) / 1000)

#ifndef MUTEX_INIT
#define MUTEX_INIT(mutex, mutex_name)			pthread_mutex_init( (pthread_mutex_t*)(&mutex), 0);
#define MUTEX_LOCK(mutex, mutex_name)			pthread_mutex_lock( (pthread_mutex_t*)(&mutex));
#define MUTEX_TRYLOCK(mutex, mutex_name, state)	state = pthread_mutex_trylock( (pthread_mutex_t*)(&mutex));
#define MUTEX_UNLOCK(mutex, mutex_name)			pthread_mutex_unlock( (pthread_mutex_t*)(&mutex));
#define MUTEX_DESTROY(mutex, mutex_name)		pthread_mutex_destroy((pthread_mutex_t*)(&mutex));
#endif

#ifndef NULL
#define NULL (void*)0
#endif

/**************************************************************************************************************************/
/* WARNING - Be sure you know you are doing if you touch these values, as they are DISK-MAPPED on METADATA representation of these objects */
typedef enum
{
	DATATYPE_DLINKEDLIST_ITEM,
	DATATYPE_DLINKEDLIST_META,
	DATATYPE_DLINKEDLIST_DATA,
	DATATYPE_DYN_ARRAY_ITEM,
	DATATYPE_DYN_ARRAY_META,
	DATATYPE_DYN_ARRAY_DATA,
	DATATYPE_DYN_BITMAP_ITEM,
	DATATYPE_DYN_BITMAP_META,
	DATATYPE_DYN_BITMAP_DATA,
	DATATYPE_HASHTABLE_ITEM,
	DATATYPE_HASHTABLE_META,
	DATATYPE_HASHTABLE_DATA,
	DATATYPE_LINKEDLIST_ITEM,
	DATATYPE_LINKEDLIST_META,
	DATATYPE_LINKEDLIST_DATA,
	DATATYPE_MEM_ARENA_ITEM,
	DATATYPE_MEM_ARENA_META,
	DATATYPE_MEM_ARENA_DATA,
	DATATYPE_MEM_BUFFER_ITEM,
	DATATYPE_MEM_BUFFER_META,
	DATATYPE_MEM_BUFFER_DATA,
	DATATYPE_MEM_STREAM_ITEM,
	DATATYPE_MEM_STREAM_META,
	DATATYPE_MEM_STREAM_DATA,
	DATATYPE_RADIXTREE_ITEM,
	DATATYPE_RADIXTREE_META,
	DATATYPE_RADIXTREE_DATA,
	DATATYPE_SLOTQUEUE_ITEM,
	DATATYPE_SLOTQUEUE_META,
	DATATYPE_SLOTQUEUE_DATA,
	DATATYPE_ASSOC_ARRAY_ITEM,
	DATATYPE_ASSOC_ARRAY_META,
	DATATYPE_ASSOC_ARRAY_DATA,
	DATATYPE_KEY_VALUE_ITEM,
	DATATYPE_KEY_VALUE_META,
	DATATYPE_KEY_VALUE_DATA,
	DATATYPE_MEMBUFFER_MAPPED_ITEM,
	DATATYPE_MEMBUFFER_MAPPED_META,
	DATATYPE_MEMBUFFER_MAPPED_DATA,
	DATATYPE_MEM_SLOT_ITEM,
	DATATYPE_MEM_SLOT_META,
	DATATYPE_MEM_SLOT_DATA,
	DATATYPE_META_DATA_ITEM,
	DATATYPE_META_DATA_META,
	DATATYPE_META_DATA_DATA,
	DATATYPE_QUEUE_ITEM,
	DATATYPE_QUEUE_META,
	DATATYPE_QUEUE_DATA,
	DATATYPE_SPEED_REGEX_ITEM,
	DATATYPE_SPEED_REGEX_META,
	DATATYPE_SPEED_REGEX_DATA,
	DATATYPE_STRING_ARRAY_ITEM,
	DATATYPE_STRING_ARRAY_META,
	DATATYPE_STRING_ARRAY_DATA,
	DATATYPE_STRING_ASSOC_ARRAY_ITEM,
	DATATYPE_STRING_ASSOC_ARRAY_META,
	DATATYPE_STRING_ASSOC_ARRAY_DATA,
	DATATYPE_STRING_ITEM,
	DATATYPE_STRING_META,
	DATATYPE_STRING_DATA,
	DATATYPE_LASTITEM,
} LibData_DataType;

typedef enum
{
	BRBDATA_THREAD_UNSAFE,
	BRBDATA_THREAD_SAFE,
	BRBATA_THREAD_LASTITEM
} LibDataThreadSafeType;

typedef void BrbDataDestroyCB(void *);

/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
/* METADATA STRUCTURES AND PROTOTYPES */
/**************************************************************************************************************************/
#define METADATA_MAGIC_HEADER				"BRB_META"
#define METADATA_MAGIC_HEADER_SZ			8
#define METADATA_UNIT_SEPARATOR				"\x1F"
#define METADATA_RECORD_SEPARATOR			"\x1E"
#define METADATA_UNIT_SEPARATOR_SZ			1

#define METADATA_ITEM_RAW_SZ				(sizeof(long) * 3)

typedef enum
{
	METADATA_UNPACK_FAILED_INVALID_HEADER_MAGIC,
	METADATA_UNPACK_FAILED_UNINITIALIZED,
	METADATA_UNPACK_FAILED_NO_RAWDATA,
	METADATA_UNPACK_FAILED_CORRUPTED_CANARY,
	METADATA_UNPACK_FAILED_DIGEST_INVALID,
	METADATA_UNPACK_FAILED_NEED_MORE_DATA_METAITEM,
	METADATA_UNPACK_FAILED_NEED_MORE_DATA_OBJECT,
	METADATA_UNPACK_SUCCESS,
	METADATA_UNPACK_LASTITEM
} MetaDataUnpackReturnCode;

/* WARNING - Be sure you known what you are doing if you TOUCH the ORDER OF THESE ITEMS - THEY WILL BE SERIALIZED IN META_DATA */
typedef struct _MetaDataItem
{
	unsigned long item_id;
	unsigned long item_sub_id;
	unsigned long sz;
	void *ptr;
} MetaDataItem;

/* WARNING - This META_HEADER must always have 64 bytes */
typedef struct _MetaDataHeader
{
	int version;
	int item_count;
	unsigned long size;
	char str[8];
	char digest[16];
	char reserved[24];
} MetaDataHeader;

typedef struct _MetaDataUnpackerInfo
{
	int error_code;
	unsigned long cur_offset;
	unsigned long cur_remaining;
	unsigned long cur_needed;

	struct
	{
		unsigned long max_item_count;
	} control;

} MetaDataUnpackerInfo;

typedef struct _MetaData
{
	struct _MemBuffer *raw_data;
	int item_offset;

	struct
	{
		struct _MemArena *arena;
		int count;
	} items;

	struct
	{
		unsigned int initialized:1;
	} flags;

} MetaData;

MetaData *MetaDataNew(void);
int MetaDataInit(MetaData *meta_data);
int MetaDataDestroy(MetaData *meta_data);
int MetaDataReset(MetaData *meta_data);
int MetaDataClean(MetaData *meta_data);
int MetaDataPack(MetaData *meta_data, struct _MemBuffer *meta_data_mb);
int MetaDataUnpack(MetaData *meta_data, struct _MemBuffer *meta_data_mb, MetaDataUnpackerInfo *unpacker_info);
int MetaDataItemAdd(MetaData *meta_data, unsigned long item_id, unsigned long item_sub_id, void *data_ptr, unsigned long data_sz);
MetaDataItem *MetaDataItemGrabMetaByID(MetaData *meta_data, unsigned long item_id, unsigned long item_sub_id);
void *MetaDataItemFindByID(MetaData *meta_data, unsigned long item_id, unsigned long item_sub_id);
/**************************************************************************************************************************/
/* DYNAMIC ARRAY STRUCTURES AND PROTOTYPES */
/**************************************************************************************************************************/
#define DYNARRAY_FOREACH(array, item) \
		for(volatile int _keep_ = 1, _count_ = 0, _size_ = array->elements; _keep_ && (_count_ < _size_); _keep_ = !_keep_, _count_++) \
		for(item = DynArrayGetDataByPos(array, _count_); _keep_; _keep_ = !_keep_)
/*************************************************************************/
typedef void ITEMDESTROY_FUNC(void *);

typedef struct _DynArray
{
	unsigned int arr_type;
	unsigned int grow_rate;
	unsigned int busy_flags;
	unsigned int elements;
	unsigned int capacity;
	pthread_mutex_t mutex;
	ITEMDESTROY_FUNC *itemdestroy_func;
	void **data;
} DynArray;
/*************************************************************************/
typedef enum {
	DYNARRAY_ACQUIRE_FAIL = -1,
	DYNARRAY_ACQUIRE_SUCCESS = 1

} DynArrayAquireStatus;
/*************************************************************************/
typedef enum {
	DYNARRAY_UNINITIALIZED = -1,
	DYNARRAY_INIT,
	DYNARRAY_BUSY,
	DYNARRAY_FREE
} DynArrayStatus;
/*************************************************************************/
//typedef enum {
//	DYNARRAY_MT_UNSAFE,
//	DYNARRAY_MT_SAFE
//} DynArrayType;
/*************************************************************************/
void *DynArrayGetDataByPos(DynArray *dyn_arr, int pos);
int DynArrayAdd(DynArray *dyn_arr, void *new_data);
DynArray *DynArrayNew(LibDataThreadSafeType arr_type, int grow_rate, ITEMDESTROY_FUNC *itemdestroy_func);
int DynArrayDestroy(DynArray *dyn_arr);
int _DynArrayEnterCritical(DynArray *dyn_arr);
int _DynArrayLeaveCritical(DynArray *dyn_arr);
int DynArrayClean(DynArray *dyn_arr);
int DynArraySetNullByPos(DynArray *dyn_arr, int pos);
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
/* LINKED LIST STRUCTURES AND PROTOTYPES */
/**************************************************************************************************************************/
typedef struct _LinkedList
{
	struct _LinkedList *next;
	void *data;
} LinkedList;

//#define for_each_item(item, list) \
//    for(T * item = list->head; item != NULL; item = item->next)

/*************************************************************************/
typedef int (*equalFunction)(void *,void *);
/*************************************************************************/
LinkedList *LinkedListNew();
LinkedList *LinkedListInsertTail(LinkedList *list, void *data);
LinkedList *LinkedListInsertHead(LinkedList *list, void *data);
LinkedList *LinkedListNext(LinkedList *elem);
LinkedList *LinkedListRemove(LinkedList *list, void *data, equalFunction f);
LinkedList *LinkedListMerge (LinkedList *head, LinkedList *tail);
LinkedList *LinkedListSort (LinkedList *lst, equalFunction f, int desc);
int LinkedListHasNext (LinkedList *list);
int LinkedListExist (LinkedList *list, void *data, equalFunction f);
int LinkedListGetSize (LinkedList *list);
void *LinkedListGetData(LinkedList *elem);
void LinkedListNodeDestroy (LinkedList *list);
void LinkedListDestroy (LinkedList * list);
/**************************************************************************************************************************/
//
//
/**************************************************************************************************************************/
/* DOUBLE LINKED LIST STRUCTURES AND PROTOTYPES */
/**************************************************************************************************************************/
typedef enum
{
	DLINKEDLIST_SORT_DESCEND,
	DLINKEDLIST_SORT_ASCEND
} DLinkedListSortCodes;
/*************************************************************************/
#define DLINKED_LIST_PTR_ISEMPTY(list)	((list)->head == NULL)
#define DLINKED_LIST_ISEMPTY(list)	((list).head == NULL)
#define DLINKED_LIST_HEAD(list)		((list).head->data)
#define	DLINKED_LIST_ISORPHAN(list)	((list).next == NULL && (list).prev == NULL )

typedef struct _DLinkedListNode
{
	void *data;
	struct _DLinkedListNode *prev;
	struct _DLinkedListNode *next;
} DLinkedListNode;

typedef struct _DLinkedList
{
	DLinkedListNode *head;
	DLinkedListNode *tail;
	pthread_mutex_t mutex;
	unsigned long size;

	struct
	{
		unsigned int thread_safe:1;
	} flags;

} DLinkedList;


typedef int DLinkedListCompareFunc(DLinkedListNode *, DLinkedListNode *);
typedef int DLinkedListFilterNode(DLinkedListNode *node, char *filter_key, char *filter_node);

void DLinkedListInit(DLinkedList *list, LibDataThreadSafeType type);
void DLinkedListReset(DLinkedList *list);
void DLinkedListAddDebug(DLinkedList *list, DLinkedListNode *node, void *data);
void DLinkedListAdd(DLinkedList *list, DLinkedListNode *node, void *data);
void DLinkedListAddTailDebug(DLinkedList *list, DLinkedListNode *node, void *data);
void DLinkedListAddTail(DLinkedList *list,  DLinkedListNode *node, void *data);
void DLinkedListDeleteDebug(DLinkedList *list, DLinkedListNode *node);
void DLinkedListDelete(DLinkedList *list, DLinkedListNode *node);
void *DLinkedListPopTail(DLinkedList *list);
void *DLinkedListPopHead(DLinkedList *list);
void DLinkedListDup(DLinkedList *list, DLinkedList *ret_list);
void DLinkedListDupFilter(DLinkedList *list, DLinkedList *ret_list, DLinkedListFilterNode *filter_func, char *filter_key, char *filter_value);
void DLinkedListClean(DLinkedList *list);
void DLinkedListDestroyData(DLinkedList *list);
void DLinkedListSort(DLinkedList *list, DLinkedList *ret_list, DLinkedListCompareFunc *cmp_func, DLinkedListSortCodes cmp_flag);
void DLinkedListSortFilter(DLinkedList *list, DLinkedList *ret_list, DLinkedListCompareFunc *cmp_func, DLinkedListSortCodes cmp_flag, DLinkedListFilterNode *filter_func, char *filter_key, char *filter_value);
void DLinkedListBubbleSort(DLinkedList *list, DLinkedList *ret_list, DLinkedListCompareFunc *cmp_func, DLinkedListSortCodes cmp_flag);
void DLinkedListMergeSort(DLinkedList *list, DLinkedList *ret_list,  DLinkedListCompareFunc *cmp_func, DLinkedListSortCodes cmp_flag);


void DLinkedListSortSimple(DLinkedList *list, DLinkedListCompareFunc *cmp_func, DLinkedListSortCodes cmp_flag);
void DLinkedListSortBubble(DLinkedList *list, DLinkedListCompareFunc *cmp_func, DLinkedListSortCodes cmp_flag);
void DLinkedListSortMerge(DLinkedList *list, DLinkedListCompareFunc *cmp_func, DLinkedListSortCodes cmp_flag);

/**************************************************************************************************************************/
/* MemBuffer STRUCTURES AND PROTOTYPES */
/**************************************************************************************************************************/
#define MEMBUFFER_MAX_PRINTF 65535
/*************************************************************************/
typedef struct _MemBufferMetaData
{
	unsigned int grow_rate;
	unsigned int data_type;
	unsigned int data_state;
	unsigned long offset;
	unsigned long size;

	int user_int;
	long user_long;
} MemBufferMetaData;

typedef struct _MemBuffer
{
	DLinkedListNode node;
	MemBufferMetaData metadata;
	pthread_mutex_t mutex;

	unsigned int mb_type;
	unsigned int grow_rate;
	unsigned int busy_flags;
	unsigned int data_type;
	unsigned int data_state;

	unsigned long size;
	unsigned long capacity;
	unsigned long offset;

	int mmap_fd;
	int ref_count;
	void *data;

	void *user_data;
	int user_int;
	long user_long;

	struct
	{
		unsigned int destroyed:1;
		unsigned int readonly:1;
		unsigned int mmaped:1;
		unsigned int wired:1;
		unsigned int no_core:1;
	} flags;
} MemBuffer;
/*************************************************************************/
typedef enum
{
	MEMBUFFER_ACQUIRE_FAIL = -1,
	MEMBUFFER_ACQUIRE_SUCCESS = 1
} MemBufferAquireStatus;
/*************************************************************************/
typedef enum
{
	MEMBUFFER_UNINITIALIZED = -1,
	MEMBUFFER_INIT,
	MEMBUFFER_BUSY,
	MEMBUFFER_FREE
} MemBufferStatus;
/*************************************************************************/
typedef enum
{
	MEMBUFFER_CLEAR,
	MEMBUFFER_COMPACTED,
	MEMBUFFER_ENCRYPTED,
} MemBufferDataState;
/*************************************************************************/
MemBuffer *MemBufferNew(LibDataThreadSafeType mb_type, int grow_rate);
int MemBufferDestroy(MemBuffer *mb_ptr);
int MemBufferClean(MemBuffer *mb_ptr);
int MemBufferShrink(MemBuffer *mb_ptr);
int MemBufferLock(MemBuffer *mb_ptr);
int MemBufferUnlock(MemBuffer *mb_ptr);
unsigned long MemBufferGetSize(MemBuffer *mb_ptr);
unsigned long MemBufferGetCapacity(MemBuffer *mb_ptr);
int MemBufferShrinkToToken(MemBuffer *mb_ptr, char token);
void MemBufferRemoveLastChar(MemBuffer *mb_ptr);
void MemBufferPutNULLTerminator(MemBuffer *mb_ptr);
int MemBufferIsInit(MemBuffer *mb_ptr);
MemBuffer *MemBufferDup(MemBuffer *mb_ptr);
MemBuffer *MemBufferDupOffset(MemBuffer *mb_ptr, unsigned long offset);
int MemBufferAppendNULL(MemBuffer *mb_ptr);
unsigned long MemBufferAdd(MemBuffer *mb_ptr, void *new_data, unsigned long new_data_sz);
MemBuffer *MemBufferMerge(MemBuffer *mb1_ptr, MemBuffer *mb2_ptr);
int MemBufferPrintf(MemBuffer *mb_ptr, char *message, ...);
int MemBufferSyncWriteToFile(MemBuffer *mb_ptr, char *filepath);
int MemBufferWriteToFile(MemBuffer *mb_ptr, char *filepath);
int MemBufferPWriteToFD(MemBuffer *mb_ptr, unsigned long mb_offset, unsigned long file_offset, int file_fd);
int MemBufferPWriteToFile(MemBuffer *mb_ptr, unsigned long mb_offset, unsigned long file_offset, char *filepath);
int MemBufferOffsetWrite(MemBuffer *mb_ptr, unsigned long offset, void *data, unsigned long data_sz);
int MemBufferOffsetWriteToFile(MemBuffer *mb_ptr, unsigned long offset, char *filepath);
MemBuffer *MemBufferReadOnlyMmapFile(char *filepath);
MemBuffer *MemBufferReadFromFile(char *filepath);
long MemBufferReadFromFileOffseted(MemBuffer *mb_ptr, unsigned long file_offset, unsigned long data_sz, char *filepath);
long MemBufferPReadFromFD(MemBuffer *mb_ptr, unsigned long file_offset, unsigned long data_sz, int fd);
long MemBufferReadFromFD(MemBuffer *mb_ptr, unsigned long data_sz, int fd);
long MemBufferAppendFromFD(MemBuffer *mb_ptr, unsigned long data_sz, int fd, int flags);
int MemBufferAppendFromFile(MemBuffer *mb_ptr, char *filepath);
int MemBufferShow(MemBuffer *mb_ptr, FILE *fd);
void *MemBufferDeref(MemBuffer *mb_ptr);
void *MemBufferOffsetDeref(MemBuffer *mb_ptr, unsigned long offset);
unsigned long MemBufferOffsetSet(MemBuffer *mb_ptr, unsigned long offset);
unsigned long MemBufferOffsetGet(MemBuffer *mb_ptr);
unsigned long MemBufferOffsetReset(MemBuffer *mb_ptr);
unsigned long MemBufferOffsetIncrement(MemBuffer *mb_ptr, unsigned long offset);
void MemBufferDecryptData(MemBuffer *mb_ptr, unsigned int seed, unsigned long offset);
void MemBufferEncryptData(MemBuffer *mb_ptr, unsigned int seed, unsigned long offset);
int _MemBufferEnterCritical(MemBuffer *mb_ptr);
int _MemBufferLeaveCritical(MemBuffer *mb_ptr);

int MemBufferNoCoreOnCrash(MemBuffer *mb_ptr);
int MemBufferUnwirePages(MemBuffer *mb_ptr);
int MemBufferWirePages(MemBuffer *mb_ptr);

int MemBufferMetaDataPack(MemBuffer *mb_ptr, MetaData *dst_metadata, unsigned long item_sub_id);
int MemBufferMetaDataUnPack(MemBuffer *mb_ptr, MemBuffer *raw_metadata_mb);
MemBuffer *MemBufferMetaDataPackToMB(MemBuffer *mb_ptr, unsigned long item_sub_id);
MemBuffer *MemBufferMetaDataUnPackFromMB(MemBuffer *raw_metadata_mb);


/**************************************************************************************************************************/
//
//
/**************************************************************************************************************************/
/* STRING ARRAY STRUCTURES AND PROTOTYPES */
/**************************************************************************************************************************/
#define MAX_ARRLINE_SZ 65535
/*************************************************************************/
#define STRINGARRAY_FOREACH_FROMINDEX(array, index, item, size) \
		for(volatile int _keep_ = 1, _count_ = index, _size_ = array->data->elements; \
		_keep_ && (_count_ < _size_); \
		_keep_ = !_keep_, _count_++) \
		for(item = StringArrayGetDataByPos(array, _count_), \
				size = StringArrayGetDataSizeByPos(array, _count_); _keep_; _keep_ = !_keep_)

#define STRINGARRAY_FOREACH(array, item, size) \
		for(volatile int _keep_ = 1, _count_ = 0, _size_ = array->data->elements; \
		_keep_ && (_count_ < _size_); \
		_keep_ = !_keep_, _count_++) \
		for(item = StringArrayGetDataByPos(array, _count_), \
				size = StringArrayGetDataSizeByPos(array, _count_); _keep_; _keep_ = !_keep_)
/*************************************************************************/
typedef int StringArrayProcessBufferLineCBH(void *cb_data, char *line_str, int line_sz);
/*************************************************************************/
typedef struct _StringArrayData
{
	unsigned int elements;
	unsigned int capacity;
	int *basearr;
	int *offsetarr;
	MemBuffer *mem_buf;
} StringArrayData;
/*************************************************************************/
typedef struct _StringArray
{
	DLinkedListNode node;
	unsigned int arr_type;
	unsigned int grow_rate;
	unsigned int busy_flags;
	pthread_mutex_t mutex;

	/* MUST BE LAST - See StringArrayNew */
	StringArrayData *data;
} StringArray;
/*************************************************************************/
typedef enum
{
	STRINGARRAY_ACQUIRE_FAIL = -1,
	STRINGARRAY_ACQUIRE_SUCCESS = 1

} StringArrayAquireStatus;
/*************************************************************************/
typedef enum
{
	STRINGARRAY_UNINITIALIZED = -1,
	STRINGARRAY_INIT,
	STRINGARRAY_BUSY,
	STRINGARRAY_FREE
} StringArrayStatus;
/*************************************************************************/
void StringArrayStripFromEmptyPos(StringArray *str_arr);
int StringArrayPrintf(StringArray *str_arr, char *message, ...);
void StringArrayClean(StringArray *str_arr);
StringArray *StringArrayNew(LibDataThreadSafeType arr_type, int grow_rate);
int StringArrayDestroy(StringArray *str_arr);
int _StringArrayCheckForGrow(StringArray *str_arr);
int StringArrayAdd(StringArray *str_arr, char *new_string);
int StringArrayAddN(StringArray *str_arr, char *new_string, int size);
int _StringArrayEnterCritical(StringArray *str_arr);
int _StringArrayLeaveCritical(StringArray *str_arr);
char *StringArrayGetDataByPos(StringArray *str_arr, int pos);
int StringArrayGetDataSizeByPos(StringArray *str_arr, int pos);
int StringArrayDeleteByPos(StringArray *str_arr, int pos);
void StringArrayDebugShow(StringArray *str_arr, FILE *fd);
void StringArrayExplodeStrInit(StringArray *arr, char *str_ptr, char *delim, char *escape, char *comment);
StringArray *StringArrayExplodeStrN(char *str_ptr, char *delim, char *escape, char *comment, int str_max_sz);
StringArray *StringArrayExplodeLargeStrN(char *str_ptr, char *delim, char *escape, char *comment, int str_max_sz);
StringArray *StringArrayExplodeStr(char *str_ptr, char *delim, char *escape, char *comment);
StringArray *StringArrayExplodeFromFile(char *filepath, char *delim, char *escape, char *comment);
StringArray *StringArrayExplodeFromMemBuffer(MemBuffer *mb_ptr, char *delim, char *escape, char *comment);
int StringArrayEscapeString(char *ret_ptr, char *str_ptr, char delim_byte, char escape_byte);
MemBuffer *StringArrayImplodeToMemBuffer(StringArray *str_arr, char *delim, char *escape);
char *StringArrayImplodeToStr(StringArray *str_arr, char *delim, char *escape);
int StringArrayImplodeToFile(StringArray *str_arr, char *filepath, char *delim, char *escape);
int StringArrayTrim(StringArray *str_arr);
int StringArrayGetPartialPosLineFmt(StringArray *str_arr, char *data, ...);
int StringArrayGetPartialPosLine(StringArray *str_arr, char *data);
int StringArrayGetPosLine(StringArray *str_arr, char *data);
int StringArrayGetPosLinePrefix(StringArray *str_arr, char *data);
int StringArrayGetPosLinePrefixFmt(StringArray *str_arr, char *data, ...);
int StringArrayGetPosLineFmt(StringArray *str_arr, char *data, ...);
int StringArrayStrStr(StringArray *str_arr, char *data);
int StringArrayLineHasPrefix(StringArray *str_arr, char *data);
int StringArrayHasLine(StringArray *str_arr, char *data);
int StringArrayHasLineFmt(StringArray *str_arr, char *data, ...);
StringArray *StringArrayDup(StringArray *str_arr);
int StringArrayUnique(StringArray *str_arr);
int StringArrayGetElemCount(StringArray *str_arr);
void StringArrayStripCRLF(StringArray *str_arr);
int StringArrayProcessBufferLines(char *buffer_str, int buffer_sz, StringArrayProcessBufferLineCBH *cb_handler_ptr, void *cb_data, int min_sz, char *delim);
char *StringArraySearchVarIntoStrArr(StringArray *data_strarr, char *target_key_str);
/**************************************************************************************************************************/
//
//
/**************************************************************************************************************************/
/* STRING STRUCTURES AND PROTOTYPES */
/**************************************************************************************************************************/
typedef MemBuffer SafeString;
/*************************************************************************/
SafeString *SafeStringNew(LibDataThreadSafeType str_type, int grow_rate);
int SafeStringClean(SafeString *str_ptr);
int SafeStringGetSize(SafeString *str_ptr);
void SafeStringDestroy(SafeString *str_ptr);
int SafeStringAdd(SafeString *str_ptr, char *data, int data_sz);
SafeString *SafeStringInitFmt(LibDataThreadSafeType str_type, char *data, ...);
SafeString *SafeStringInit(LibDataThreadSafeType str_type, char *data);
char *SafeStringDeref(SafeString *str_ptr);
int SafeStringAppend(SafeString *str_ptr, char *data);
SafeString *SafeStringDup(SafeString *str_ptr);
int SafeStringPrintf(SafeString *str_ptr, char *message, ...);
SafeString *SafeStringReplaceSubStrDup(SafeString *str_ptr, char *sub_str, char *new_sub_str, int elem_count);
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
/* SPEED REGEX STRUCTURES AND PROTOTYPES */
/**************************************************************************************************************************/
typedef struct _SpeedRegEx
{
	unsigned int order_match;
	unsigned int spdreg_type;
	unsigned int wants_begin_with;
	unsigned int wants_finish_with;
	StringArray *match_elem_arr;
} SpeedRegEx;
/*************************************************************************/
typedef enum {
	SPDREGEX_MT_UNSAFE,
	SPDREGEX_MT_SAFE
} SpeedRegexType;
/*************************************************************************/
typedef enum {
	SPDREGEX_ORDER_SENSE,
	SPDREGEX_ORDER_INSENSE
} SpeedRegexOrder;
/*************************************************************************/
SpeedRegEx *SpeedRegExNew(SpeedRegexType type);
void SpeedRegExDestroy (SpeedRegEx *speedreg);
SpeedRegEx *SpeedRegExCompile(char *speed_regex, int order_match, int type);
int SpeedRegExExecute (SpeedRegEx *speedreg, char *data);
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
/* BRB REGEX STRUCTURES AND PROTOTYPES */
/**************************************************************************************************************************/
#define BRB_REGEX_MATCH 10
typedef struct _BrbRegEx
{
	regex_t reg;
	regmatch_t match[BRB_REGEX_MATCH];

	struct {
		unsigned int compiled:1;
	} flags;
} BrbRegEx;
/*************************************************************************/
int BrbRegExCompile(BrbRegEx *brb_regex, char *regex_str);
int BrbRegExCompare(BrbRegEx *brb_regex, char *cmp_str);
int BrbRegExReplace(BrbRegEx *brb_regex, char *orig_str, char *sub_str, char *buf_str, int buf_sz);
int BrbRegExClean(BrbRegEx *brb_regex);
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
/* MD5 STRUCTURES AND PROTOTYPES */
/**************************************************************************************************************************/
#define F1(x, y, z) (z ^ (x & (y ^ z)))
#define F2(x, y, z) F1(z, x, y)
#define F3(x, y, z) (x ^ y ^ z)
#define F4(x, y, z) (y ^ (x | ~z))
/*************************************************************************/
/* This is the central step in the MD5 algorithm. */
#define MD5STEP(f,w,x,y,z,in,s) (w += f(x,y,z) + in, w = (w<<s | w>>(32-s)) + x)
#define MD5_DIGEST_LENGTH         16
/*************************************************************************/
typedef struct _BRB_MD5_CTX {
	uint32_t buf[4];
	uint32_t bytes[2];
	uint32_t in[16];
	unsigned char digest[16];
	unsigned char string[64];
} BRB_MD5_CTX;
/*************************************************************************/
void BRB_MD5Init(BRB_MD5_CTX *ctx);
void BRB_MD5UpdateBig(BRB_MD5_CTX *ctx, const void *_buf, unsigned long len);
void BRB_MD5Update(BRB_MD5_CTX *ctx, const void *_buf, unsigned long len);
void BRB_MD5Final(BRB_MD5_CTX *ctx);
void BRB_MD5Transform(BRB_MD5_CTX *ctx);
void BRB_MD5LateInitDigestString(BRB_MD5_CTX *ret);
void BRB_MD5ToStr(unsigned char *bin_digest,  unsigned char *ret_buf_str);
/**************************************************************************************************************************/
//
//
/**************************************************************************************************************************/
/* BLOWFISH STRUCTURES AND PROTOTYPES */
/**************************************************************************************************************************/
typedef struct _BRB_BLOWFISH_CTX {
	unsigned long P[16 + 2];
	unsigned long S[4][256];
} BRB_BLOWFISH_CTX;
/*************************************************************************/
void BRB_Blowfish_Init(BRB_BLOWFISH_CTX *ctx, unsigned char *key, int keyLen);
void BRB_Blowfish_Encrypt(BRB_BLOWFISH_CTX *ctx, unsigned long *xl, unsigned long *xr);
void BRB_Blowfish_Decrypt(BRB_BLOWFISH_CTX *ctx, unsigned long *xl, unsigned long *xr);
/**************************************************************************************************************************/
/* RC4 STREAM CYPHER STRUCTURES AND PROTOTYPES */
/**************************************************************************************************************************/
typedef struct _BRB_RC4_State
{
	unsigned char perm[256];
	unsigned char index1;
	unsigned char index2;

	struct
	{
		unsigned int initialized:1;
	} flags;
} BRB_RC4_State;

void BRB_RC4_Init(BRB_RC4_State *state, const unsigned char *key, int keylen);
void BRB_RC4_Crypt(BRB_RC4_State *state, const unsigned char *inbuf, unsigned char *outbuf, int buflen);
/**************************************************************************************************************************/
/* HashTable STRUCTURES AND PROTOTYPES */
/**************************************************************************************************************************/
/*
 *  Here are some good prime number choices.  It's important not to
 *  choose a prime number that is too close to exact powers of 2.
 *
 *  HASH_SIZE 103               // prime number < 128
 *  HASH_SIZE 229               // prime number < 256
 *  HASH_SIZE 467               // prime number < 512
 *  HASH_SIZE 977               // prime number < 1024
 *  HASH_SIZE 1979              // prime number < 2048
 *  HASH_SIZE 4019              // prime number < 4096
 *  HASH_SIZE 6037              // prime number < 6144
 *  HASH_SIZE 7951              // prime number < 8192
 *  HASH_SIZE 12149             // prime number < 12288
 *  HASH_SIZE 16231             // prime number < 16384
 *  HASH_SIZE 33493             // prime number < 32768
 *  HASH_SIZE 65357             // prime number < 65536
 */
#define  DEFAULT_HASH_SIZE 65357	/* prime number < 8192 */
#define HASH_PRIME1		37
#define HASH_PRIME2		1048583
/*************************************************************************/
typedef void HashFreeFunc(void *);
typedef int HashCmpFunc(const void *, const void *);
typedef unsigned int HashHashFunc(const void *, unsigned int);
/*************************************************************************/
typedef struct _BRBHashTableItem
{
	char *key;
	struct _BRBHashTableItem *next;
	void *item;
} BRBHashTableItem;
/*************************************************************************/
typedef struct _BRBHashTable
{
	int valid;
	BRBHashTableItem **buckets;
	HashCmpFunc *cmp;
	HashHashFunc *hash;
	unsigned int size;
	unsigned int current_slot;
	BRBHashTableItem *current_ptr;
	BRBHashTableItem *next;
	int count;
} BRBHashTable;
/*************************************************************************/
BRBHashTable *HashTableNew(HashCmpFunc *, int, HashHashFunc *);
void HashTablePrint(BRBHashTable *hid);
void HashTableAddItem(BRBHashTable * hid, const char *k, void *item);
void HashTableJoinHashItem(BRBHashTable *, BRBHashTableItem *);
int HashTableRemoveItem(BRBHashTable * hid, BRBHashTableItem * hl, int FreeLink);
int HashTablePrime(int n);
BRBHashTableItem *HashTableLookup(BRBHashTable *, const void *);
void *HashTableFirstItem(BRBHashTable *);
void *HashTableNextItem(BRBHashTable *);
void HashTableLastItem(BRBHashTable *);
BRBHashTableItem *HashTableGetBucket(BRBHashTable *, unsigned int);
void HashTableFreeMemory(BRBHashTable *);
void HashTableFreeItems(BRBHashTable *, HashFreeFunc *);
const char *HashTableHashKeyStr(BRBHashTableItem *);
/*************************************************************************/
/* Internal hash functions
/*************************************************************************/
HashHashFunc HashTableLongNumberHash;
HashHashFunc HashTableMurMurHash;
HashHashFunc HashTableStringHash;
HashHashFunc HashTableHash4;
HashHashFunc HashTableURLHash;
/**************************************************************************************************************************/
//
//
/**************************************************************************************************************************/
/* HashTableGC STRUCTURES AND PROTOTYPES */
/**************************************************************************************************************************/
typedef struct _HashTableGCConfig
{
	int a;
} HashTableGCConfig;

typedef struct _HashTableGC
{
	DLinkedList list;
	struct _MemArena *arena;
	unsigned int modulo;
	unsigned int key_sz;

	struct
	{
		HashFreeFunc *destroy;
		HashHashFunc *hash;
		HashCmpFunc *cmp;
	} func;

	struct
	{
		unsigned int loaded:1;
		unsigned int thread_safe:1;
	} flags;
} HashTableGC;

typedef struct _HashTableGCItem
{
	DLinkedListNode node;
	struct _HashTableGCItem *next;
	void *key;
	void *data;
} HashTableGCItem;

/**************************************************************************************************************************/
/* StringAssocArray STRUCTURES AND PROTOTYPES */
/**************************************************************************************************************************/
typedef enum
{
	STRINGASSOCARRAY_ACQUIRE_FAIL = -1,
	STRINGASSOCARRAY_ACQUIRE_SUCCESS = 1
} StringAssocArrayAquireStatus;
/*************************************************************************/
typedef enum
{
	STRINGASSOCARRAY_UNINITIALIZED = -1,
	STRINGASSOCARRAY_INIT,
	STRINGASSOCARRAY_BUSY,
	STRINGASSOCARRAY_FREE
} StringAssocArrayStatus;
/*************************************************************************/
typedef enum
{
	STRINGBRBDATA_THREAD_UNSAFE,
	STRINGBRBDATA_THREAD_SAFE
} StringAssocArrayType;
/*************************************************************************/
typedef struct _StringAssocArray
{
	BRBHashTable *hash_table;
	unsigned int elem_count;
} StringAssocArray;
/*************************************************************************/
StringAssocArray *StringAssocArrayNew(int capacity);
void StringAssocArrayAdd(StringAssocArray *string_assoc_array, char *str_key, char *str_value);
char *StringAssocArrayLookup(StringAssocArray *string_assoc_array, char *str_key);
int StringAssocArrayDelete(StringAssocArray *string_assoc_array, char *str_key);
void StringAssocArrayDestroy(StringAssocArray *string_assoc_array);
void StringAssocArrayDebugShow(StringAssocArray *string_assoc_array, FILE *fd);
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
/* AssocArray STRUCTURES AND PROTOTYPES */
/**************************************************************************************************************************/
#define ASSOCARR_MUTEX_INIT(assoc_arr)		if (assoc_arr->arr_type == BRBDATA_THREAD_SAFE) { \
		pthread_mutex_init( (pthread_mutex_t*)(&assoc_arr->mutex), 0); }

#define ASSOCARR_MUTEX_DESTROY(assoc_arr)	if (assoc_arr->arr_type == BRBDATA_THREAD_SAFE) { \
		pthread_mutex_destroy( (pthread_mutex_t*)(&assoc_arr->mutex)); }

#define ASSOCARR_MUTEX_LOCK(assoc_arr)		if (assoc_arr->arr_type == BRBDATA_THREAD_SAFE) { \
		pthread_mutex_lock((pthread_mutex_t*)(&assoc_arr->mutex)); \
		ASSOCARR_LOCKSTATE_SETLOCKED(assoc_arr); }

#define ASSOCARR_MUTEX_UNLOCK(assoc_arr)	if (assoc_arr->arr_type == BRBDATA_THREAD_SAFE) { \
		pthread_mutex_unlock((pthread_mutex_t*)(&assoc_arr->mutex)); \
		ASSOCARR_LOCKSTATE_SETUNLOCKED(assoc_arr); }


#define ASSOCARR_BUSYSTATE_ISFREE(assoc_arr)  EBIT_TEST(assoc_arr->busy_flags, ASSOCARRAY_FREE);
#define ASSOCARR_BUSYSTATE_SETFREE(assoc_arr) EBIT_CLR(assoc_arr->busy_flags, ASSOCARRAY_BUSY); \
		EBIT_SET(assoc_arr->busy_flags, ASSOCARRAY_FREE);

#define ASSOCARR_BUSYSTATE_ISBUSY(assoc_arr)  EBIT_TEST(assoc_arr->busy_flags, ASSOCARRAY_BUSY);
#define ASSOCARR_BUSYSTATE_SETBUSY(assoc_arr) EBIT_CLR(assoc_arr->busy_flags, ASSOCARRAY_FREE); \
		EBIT_SET(assoc_arr->busy_flags, ASSOCARRAY_BUSY);

#define ASSOCARR_LOCKSTATE_ISLOCKED(assoc_arr)  EBIT_TEST(assoc_arr->busy_flags, ASSOCARRAY_LOCKSTATE_LOCKED);
#define ASSOCARR_LOCKSTATE_SETLOCKED(assoc_arr) EBIT_SET(assoc_arr->lock_state, ASSOCARRAY_LOCKSTATE_LOCKED); \
		EBIT_CLR(assoc_arr->lock_state, ASSOCARRAY_LOCKSTATE_UNLOCKED);

#define ASSOCARR_LOCKSTATE_ISUNLOCKED(assoc_arr)  EBIT_TEST(assoc_arr->busy_flags, ASSOCARRAY_LOCKSTATE_UNLOCKED);
#define ASSOCARR_LOCKSTATE_SETUNLOCKED(assoc_arr) EBIT_CLR(assoc_arr->lock_state, ASSOCARRAY_LOCKSTATE_LOCKED); \
		EBIT_SET(assoc_arr->lock_state, ASSOCARRAY_LOCKSTATE_UNLOCKED);

#define ASSOCARR_FOREACH_BEGIN(arr, key_str, value, counter) BRBHashTableItem *walker; counter = 0; ASSOCARR_MUTEX_LOCK(arr); ASSOCARR_BUSYSTATE_SETBUSY(arr);  walker = HashTableFirstItem(arr->hash_table); \
		for (walker = HashTableNextItem(arr->hash_table); walker != NULL; walker = HashTableNextItem(arr->hash_table)) { counter++; key_str = walker->key; value = walker->item;
#define ASSOCARR_FOREACH_END(arr)  } ASSOCARR_BUSYSTATE_SETFREE(arr); ASSOCARR_MUTEX_UNLOCK(arr)
#define ASSOCARR_HAS_NEXT_ITEM(arr, counter) ((counter) < arr->hash_table->count ?  1 : 0)
/*************************************************************************/
typedef enum
{
	ASSOCARRAY_ACQUIRE_FAIL = -1,
	ASSOCARRAY_ACQUIRE_SUCCESS = 1
} AssocArrayAquireStatus;
/*************************************************************************/
typedef enum
{
	ASSOCARRAY_UNINITIALIZED = -1,
	ASSOCARRAY_INIT,
	ASSOCARRAY_BUSY,
	ASSOCARRAY_FREE
} AssocArrayStatus;
/*************************************************************************/
typedef enum
{
	ASSOCARRAY_LOCKSTATE_LOCKED,
	ASSOCARRAY_LOCKSTATE_UNLOCKED
} AssocArrayLockStates;

/*************************************************************************/
typedef void ASSOCITEM_DESTROYFUNC(void *);
/*************************************************************************/
typedef struct _AssocArray
{
	BRBHashTable *hash_table;
	unsigned int elem_count;
	ASSOCITEM_DESTROYFUNC *itemdestroy_func;

	/* MT-Safety structures */
	unsigned int busy_flags;
	unsigned int lock_state;
	unsigned int arr_type;
	pthread_mutex_t mutex;

} AssocArray;
/*************************************************************************/
AssocArray *AssocArrayNew(LibDataThreadSafeType assoc_arr_type, int capacity, ASSOCITEM_DESTROYFUNC *itemdestroy_func);
void AssocArrayAddNoCheck(AssocArray *assoc_array, char *str_key, void *value);
void AssocArrayAdd(AssocArray *assoc_array, char *str_key, void *value);
char *AssocArrayLookup(AssocArray *assoc_array, char *str_key);
void AssocArrayDebugShow(AssocArray *assoc_array, FILE *fd);
int AssocArrayDelete(AssocArray *assoc_array, char *str_key);
void AssocArrayDestroy(AssocArray *assoc_array);
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
/* Base64 STRUCTURES AND PROTOTYPES */
/**************************************************************************************************************************/
char *brb_base64_decode(const char *p);
int brb_base64_decode_bin(char *in_ptr, char *out_ptr, int out_sz);
const char *brb_base64_encode(const char *decoded_str);
const char *brb_base64_encode_bin(const char *data, int len);
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
/* KV STRUCTURES AND PROTOTYPES */
/**************************************************************************************************************************/
#define KV_ROW_MAX 8092
/* Random seed for enc key */
#define KV_SEED 0x4FD9
/*************************************************************************/
enum
{
	BRB_KV_TPL_STATE_READING_TEXT,
	BRB_KV_TPL_STATE_READING_PARAM,
	BRB_KV_TPL_STATE_READING_LASTITEM
} KVInterpStateCodes;
/*************************************************************************/
typedef struct _KV_ARRAY_DATA
{
	LinkedList *parsed_content_list;
	unsigned int parsed_content_sz;
	MemBuffer *file_data;
	MemBuffer *json_data;
} KV_ARRAY_DATA;
/*************************************************************************/
typedef struct _KEY_VALUE
{
	volatile int lock_status:1;
	char key[256];
	unsigned int key_sz;
	char value[256];
	unsigned int value_sz;
} KEY_VALUE;
/*************************************************************************/
/* PUBLIC FUNCTIONS */
/*************************************************************************/
KEY_VALUE *KVRowGetByKey(KV_ARRAY_DATA *arrayData, char *key);
int KVRowAdd(KV_ARRAY_DATA *arrayData, char *key, char *value);
int KVRowDeleteByKey(KV_ARRAY_DATA *arrayData, char *key);
int KVRowUpdateByKey(KV_ARRAY_DATA *arrayData, char *key, char *new_value);
int KVDestroy(KV_ARRAY_DATA *arrayData);
int KVWriteToFile (KV_ARRAY_DATA *arrayData, char *conf_path, int encFlag, int withoutQuotes);
KV_ARRAY_DATA *KVReadFromFile (char *conf_path, int encFlag);
int KVArrayCount(KV_ARRAY_DATA *arrayData);
int KVShow (FILE *fd, KEY_VALUE *kv_ptr);
int KVJsonExport(KV_ARRAY_DATA *arrayData);
int KvInterpTplToMemBuffer(KV_ARRAY_DATA *kv_data, char *data_str, int data_sz, MemBuffer *data_mb);
/*************************************************************************/
/* PRIVATE FUNCTIONS */
/*************************************************************************/
KEY_VALUE *__KVRowNew ();
KV_ARRAY_DATA *KVArrayNew ();
int __KVArrayParse (KV_ARRAY_DATA *arrayData);
KEY_VALUE *__KVRowParse (char *row, ...);
int __KVRemoveQuotes(char *src_ptr, char *dst_ptr);
static int __KVCompareFunc(char *target, void *data);
int __KVAssembleToMemBuffer(KV_ARRAY_DATA *arrayData, MemBuffer *asm_buffer, int withoutQuotes);
int __KVCountTokens (char *string, char token);
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
/* MEM_STREAM STRUCTURES AND PROTOTYPES */
/**************************************************************************************************************************/
#define MEMSTREAM_MUTEX_INIT(mem_stream)		if (mem_stream->stream_type == MEMSTREAM_MT_SAFE) { \
		pthread_mutex_init( (pthread_mutex_t*)(&mem_stream->mutex), 0); }

#define MEMSTREAM_MUTEX_DESTROY(mem_stream)	if (mem_stream->stream_type == MEMSTREAM_MT_SAFE) { \
		pthread_mutex_destroy( (pthread_mutex_t*)(&mem_stream->mutex)); }

#define MEMSTREAM_MUTEX_LOCK(mem_stream)		if (mem_stream->stream_type == MEMSTREAM_MT_SAFE) { \
		pthread_mutex_lock((pthread_mutex_t*)(&mem_stream->mutex)); \
		MEMSTREAM_LOCKSTATE_SETLOCKED(mem_stream); }

#define MEMSTREAM_MUTEX_UNLOCK(mem_stream)	if (mem_stream->stream_type == MEMSTREAM_MT_SAFE) { \
		pthread_mutex_unlock((pthread_mutex_t*)(&mem_stream->mutex)); \
		MEMSTREAM_LOCKSTATE_SETUNLOCKED(mem_stream); }


#define MEMSTREAM_BUSYSTATE_ISFREE(mem_stream)  EBIT_TEST(mem_stream->busy_flags, MEMSTREAM_FREE);
#define MEMSTREAM_BUSYSTATE_SETFREE(mem_stream) EBIT_CLR(mem_stream->busy_flags, MEMSTREAM_BUSY); \
		EBIT_SET(mem_stream->busy_flags, MEMSTREAM_FREE);

#define MEMSTREAM_BUSYSTATE_ISBUSY(mem_stream)  EBIT_TEST(mem_stream->busy_flags, MEMSTREAM_BUSY);
#define MEMSTREAM_BUSYSTATE_SETBUSY(mem_stream) EBIT_CLR(mem_stream->busy_flags, MEMSTREAM_FREE); \
		EBIT_SET(mem_stream->busy_flags, MEMSTREAM_BUSY);

#define MEMSTREAM_LOCKSTATE_ISLOCKED(mem_stream)  EBIT_TEST(mem_stream->busy_flags, MEMSTREAM_LOCKSTATE_LOCKED);
#define MEMSTREAM_LOCKSTATE_SETLOCKED(mem_stream) EBIT_SET(mem_stream->lock_state, MEMSTREAM_LOCKSTATE_LOCKED); \
		EBIT_CLR(mem_stream->lock_state, MEMSTREAM_LOCKSTATE_UNLOCKED);

#define MEMSTREAM_LOCKSTATE_ISUNLOCKED(mem_stream)  EBIT_TEST(mem_stream->busy_flags, MEMSTREAM_LOCKSTATE_UNLOCKED);
#define MEMSTREAM_LOCKSTATE_SETUNLOCKED(mem_stream) EBIT_CLR(mem_stream->lock_state, MEMSTREAM_LOCKSTATE_LOCKED); \
		EBIT_SET(mem_stream->lock_state, MEMSTREAM_LOCKSTATE_UNLOCKED);



/*************************************************************************/
typedef enum {
	MEMSTREAM_ACQUIRE_FAIL = -1,
	MEMSTREAM_ACQUIRE_SUCCESS = 1
} MemStreamAquireStatus;
/*************************************************************************/
typedef enum {
	MEMSTREAM_UNINITIALIZED = -1,
	MEMSTREAM_INIT,
	MEMSTREAM_BUSY,
	MEMSTREAM_FREE
} MemStreamStatus;
/*************************************************************************/
typedef enum {
	MEMSTREAM_MT_UNSAFE,
	MEMSTREAM_MT_SAFE
} MemStreamType;
/*************************************************************************/
typedef enum {
	MEMSTREAM_LOCKSTATE_LOCKED,
	MEMSTREAM_LOCKSTATE_UNLOCKED
} MemStreamLockStates;
/**************************************************************************************************************************/
#define COUNTER_UPDATE_NODE_AND_STREAM(mem_stream, mem_node, data_sz) mem_stream->data_size += data_sz; mem_node->node_sz += data_sz
#define MEM_NODE_AVAILCAP(mem_node) (mem_node->node_cap - mem_node->node_sz)
#define MEM_NODE_COPYINTO(mem_node, data, data_sz) {char *__base_ptr__ = mem_node->node_data; __base_ptr__ += mem_node->node_sz; memcpy(__base_ptr__, data, data_sz);}
#define MEM_NODE_GET_CUR_BASE(mem_node, base_ptr) {char *__base_ptr__ = mem_node->node_data; __base_ptr__ += mem_node->node_sz; base_ptr = __base_ptr__;}
#define MEM_NODE_NEW_TAILNODE(mem_stream, mem_node, data_sz) MemStreamCreateNewNodeTail(mem_stream, data_sz); mem_node = mem_stream->nodes.tail_ptr
/*************************************************************************/
typedef struct _MemStreamNode
{
	struct _MemStreamNode *next_node;
	struct _MemStreamNode *prev_node;
	unsigned int node_refcount;

	unsigned long node_sz;
	unsigned long node_cap;
	unsigned long node_off;

	void *parent_stream;
	void *node_data;

} MemStreamNode;
/*************************************************************************/
typedef struct _MemStreamRef
{
	unsigned long node_idx;
	unsigned long node_sz;
	void *node_base;
	void *data;
} MemStreamRef;
/*************************************************************************/
typedef struct _MemStream
{
	unsigned long data_size;
	unsigned long data_off;
	long min_node_sz;

	struct
	{
		unsigned long node_count;
		unsigned long node_idx;

		MemStreamNode *head_ptr;
		MemStreamNode *tail_ptr;
		MemStreamNode *cur_ptr;
	} nodes;

	/* MT-Safety structures */
	unsigned int busy_flags;
	unsigned int lock_state;
	unsigned int stream_type;
	pthread_mutex_t mutex;

} MemStream;
/*************************************************************************/
MemStreamNode *MemStreamNodeNew(MemStream *mem_stream, unsigned long node_sz);
int MemStreamNodeDestroy(MemStream *mem_stream, MemStreamNode *mem_node_ptr);
MemStream *MemStreamNew(unsigned long min_node_sz, MemStreamType type);
void MemStreamClean(MemStream *mem_stream);
void MemStreamDestroy(MemStream *mem_stream);
int MemStreamCreateNewNodeTail(MemStream *mem_stream, unsigned long data_sz);
int MemStreamGrabDataFromFD(MemStream *mem_stream, unsigned long data_sz, int fd);
void MemStreamWrite(MemStream *mem_stream, void *data, unsigned long data_sz);
void MemStreamWriteToFILE(MemStream *mem_stream, FILE *file);
void MemStreamWriteToFD(MemStream *mem_stream, int fd);
void MemStreamOffsetDeref(MemStream *mem_stream, MemStreamRef *mem_stream_ref, unsigned long offset);
void MemStreamBaseDeref(MemStream *mem_stream, MemStreamRef *mem_stream_ref);
unsigned long MemStreamGetDataSize(MemStream *mem_stream);
unsigned long MemStreamGetNodeCount(MemStream *mem_stream);
/*************************************************************************/
/* MEM ARENA */
/*************************************************************************/
typedef enum
{
	MEMARENA_MT_UNSAFE,
	MEMARENA_MT_SAFE
} MemArenaTypes;

typedef enum
{
	MEMARENA_SIZE_CURRENT,
	MEMARENA_SIZE_INITIAL,
	MEMARENA_SIZE_CAPACITY,
	MEMARENA_SIZE_LASTITEM
} MemArenaSizeTypes;

typedef enum
{
	MEMARENA_SLOT_SIZE,
	MEMARENA_SLOT_COUNT,
	MEMARENA_SLOT_LASTITEM
} MemArenaSlotTypes;

typedef struct _MemArena
{
	pthread_mutex_t mutex;
	DLinkedList slot_list;
	long size[MEMARENA_SIZE_LASTITEM];
	long slot[MEMARENA_SLOT_LASTITEM];
	char **data;
} MemArena;

typedef struct _MemArenaSlotHeader
{
	short canary00;
	DLinkedListNode node;
	struct _DynBitMap *bitmap;
	MemArena *mem_arena;
	long slot_id;
	long busy_count;
	short canary01;
} MemArenaSlotHeader;

MemArena *MemArenaNew(long arena_size, long slot_size, long slot_count, int type);
void MemArenaClean(MemArena *mem_arena);
void MemArenaDestroy(MemArena *mem_arena);
void *MemArenaGrabByID(MemArena *mem_arena, int long);
void MemArenaReleaseByID(MemArena *mem_arena, long id);
void MemArenaLockByID(MemArena *mem_arena, long id);
int MemArenaUnlockByID(MemArena *mem_arena, long id);
long MemArenaGetArenaCapacity(MemArena *mem_arena);
long MemArenaGetSlotSize(MemArena *mem_arena);
long MemArenaGetSlotCount(MemArena *mem_arena);
long MemArenaGetSlotActiveCount(MemArena *mem_arena);


/*************************************************************************/
/* QUEUE */
/*************************************************************************/

typedef enum
{
	QUEUE_MT_UNSAFE,
	QUEUE_MT_SAFE,
} QueueMTState;

typedef enum
{
	QUEUE_DO_POST,
	QUEUE_NOT_DO_POST,
} QueueSemPost;
/******************************************************************************************************/
typedef void QueueDataCBH(int, int, int, void*, void*);
typedef void QueueDataDestroyFunc(void*);
/******************************************************************************************************/
typedef struct _QueueData
{
	DLinkedListNode node;
	void *data;
	QueueDataDestroyFunc *destroy_func;
	QueueDataCBH *finish_cb;
	void *finish_cbdata;
} QueueData;

typedef struct _QueueList
{
	pthread_mutex_t mutex;
	DLinkedList list;
	long list_size;
	sem_t queue_semaphore;
	struct
	{
		unsigned int mt_engine:1;
		unsigned int mutex_init:1;
	} flags;

} QueueList;
/******************************************************************************************************/
#define QUEUE_MUTEX_INIT(queue_list) \
		if (queue_list->flags.mt_engine && !queue_list->flags.mutex_init) { \
			MUTEX_INIT (queue_list->mutex, "QUEUE_MUTEX") \
			queue_list->flags.mutex_init = 1; }

#define QUEUE_MUTEX_DESTROY(queue_list) \
		if (queue_list->flags.mt_engine && queue_list->flags.mutex_init) { \
			MUTEX_DESTROY (queue_list->mutex, "QUEUE_MUTEX") \
			queue_list->flags.mutex_init = 0; }

#define QUEUE_MUTEX_LOCK(queue_list)	\
		if (queue_list->flags.mt_engine) \
		MUTEX_LOCK (queue_list->mutex, "QUEUE_MUTEX")

#define QUEUE_MUTEX_TRYLOCK(queue_list, state) \
		if (queue_list->flags.mt_engine) \
		MUTEX_TRYLOCK (queue_list->mutex, "QUEUE_MUTEX", state)

#define QUEUE_MUTEX_UNLOCK(queue_list) \
		if (queue_list->flags.mt_engine) \
		MUTEX_UNLOCK (queue_list->mutex, "QUEUE_MUTEX")
/******************************************************************************************************/
QueueList *queueListNew(QueueMTState queue_type);
void queueListDestroy(QueueList *queue);
void queueListClean(QueueList *queue);
void queueListInit(QueueList *queue, QueueMTState queue_type);
int queueListIsEmpty(QueueList *queue);
QueueData *queueDataNew(void *item, QueueDataDestroyFunc *destroy_func, QueueDataCBH *finish_cb, void *finish_cbdata);
void queueDataEnqueue(QueueList *queue, QueueData *queue_data, QueueSemPost post);
QueueData * queueDataDequeue(QueueList *queue);
void queueDataDestroy(QueueData *queue_data);
/*************************************************************************/
/* JSON PARSER  */
/*************************************************************************/
#define MEMBUFFER_JSON_BEGIN_ARRAY(mb)					MemBufferAdd(mb, "[", 1)
#define MEMBUFFER_JSON_BEGIN_ARRAY_KEY(mb, key)			MemBufferPrintf(mb, "\"%s\": [", key)
#define MEMBUFFER_JSON_BEGIN_OBJECT(mb)					MemBufferAdd(mb, "{", 1)
#define MEMBUFFER_JSON_BEGIN_KEY(mb, key)				MemBufferPrintf(mb, "\"%s\": ", key)
#define MEMBUFFER_JSON_BEGIN_OBJECT_KEY(mb, key)		MemBufferPrintf(mb, "\"%s\": {", key)
#define MEMBUFFER_JSON_FINISH_ARRAY(mb)					MemBufferAdd(mb, "]", 1)
#define MEMBUFFER_JSON_FINISH_OBJECT(mb)				MemBufferAdd(mb, "}", 1)
#define MEMBUFFER_JSON_ADD_COMMA(mb)					MemBufferAdd(mb, ",", 1)
#define MEMBUFFER_JSON_ADD_STR(mb, key, value)			MemBufferPrintf(mb, "\"%s\": \"%s\"", key, value)
#define MEMBUFFER_JSON_ADD_BOOLEAN(mb, key, boolean)	MemBufferPrintf(mb, "\"%s\": %s", key, (boolean ? "true" : "false"))
#define MEMBUFFER_JSON_ADD_NULL(mb)						MemBufferAdd(mb, "null", 4)
#define MEMBUFFER_JSON_ADD_NULL_KEY(mb, key)			MemBufferPrintf(mb, "\"%s\": null", key)
#define MEMBUFFER_JSON_ADD_INT(mb, key, value)			MemBufferPrintf(mb, "\"%s\": %d", key, value)
#define MEMBUFFER_JSON_ADD_UINT(mb, key, value)			MemBufferPrintf(mb, "\"%s\": %u", key, value)
#define MEMBUFFER_JSON_ADD_IPPORT(mb, key, value)		MemBufferPrintf(mb, "\"%s:%d\"", key, value)
#define MEMBUFFER_JSON_ADD_LONG(mb, key, value)			MemBufferPrintf(mb, "\"%s\": %ld", key, value)
#define MEMBUFFER_JSON_ADD_ULONG(mb, key, value)		MemBufferPrintf(mb, "\"%s\": %lu", key, value)
#define MEMBUFFER_JSON_ADD_HEX(mb, key, value)			MemBufferPrintf(mb, "\"%s\": \"%08X\"", key, value)
#define MEMBUFFER_JSON_ADD_HEX_NOMASK(mb, key, value)	MemBufferPrintf(mb, "\"%s\": \"%X\"", key, value)
#define MEMBUFFER_JSON_ADD_FLOAT(mb, key, value)		MemBufferPrintf(mb, "\"%s\": %.2f", key, value)
#define MEMBUFFER_JSON_ADD_STR_FMT(mb, key, fmt, ...)	MemBufferPrintf(mb, "{ \"text\": \"%s\", \"value\": "fmt", \"leaf\": true }", key, ##__VA_ARGS__)

/*************************************************************************/
/* RADIX TREE  */
/*************************************************************************/
#define BIT_TEST(f, b)  ((f) & (b))
#define RADIX_PREFIX_TO_CHAR(prefix)	((unsigned char *)&(prefix)->add)
#define RADIX_PREFIX_TO_UCHAR(prefix)	((unsigned char *)&(prefix)->add)

#define RADIX_MAXBITS 128
#define RADIX_WALK(Xhead, Xnode) do { RadixNode *Xstack[RADIX_MAXBITS+1]; RadixNode **Xsp = Xstack; RadixNode *Xrn = (Xhead); while ((Xnode = Xrn)) { if (Xnode->prefix)
#define RADIX_WALK_END 	if (Xrn->l) { if (Xrn->r) { *Xsp++ = Xrn->r; } Xrn = Xrn->l; } else if (Xrn->r) { Xrn = Xrn->r; } \
		else if (Xsp != Xstack) { Xrn = *(--Xsp); } else { Xrn = (RadixNode *) 0; }}} while (0)

typedef struct _RadixPrefix
{
	unsigned int family;			/* AF_INET | AF_INET6 */
	unsigned int bitlen;			/* same as mask? */
	int ref_count;			/* reference count */
	union {
		struct in_addr sin;
		struct in6_addr sin6;
	} add;
} RadixPrefix;

typedef struct _RadixNode
{
	unsigned int bit;			/* flag if this node used */
	RadixPrefix *prefix;		/* who we are in radix tree */
	struct _RadixNode *l, *r;	/* left and right children */
	struct _RadixNode *parent;	/* may be used */
	void *data;			/* pointer to data */
} RadixNode;

typedef struct _RadixTree
{
	RadixNode *head;
	unsigned int maxbits;			/* for IP, 32 bit addresses */
	int num_active_node;		/* for debug purpose */
} RadixTree;

typedef void *RadixCBType (RadixNode *, void *);

int RadixPrefixInit(RadixPrefix *prefix, unsigned int af_family, void *dest, unsigned int bitlen);
RadixTree *RadixTreeNew(void);
void RadixPrefixDeref(RadixPrefix *prefix);
RadixPrefix *RadixPrefixNew(int family, void *dest, int bitlen, RadixPrefix *prefix);
void RadixTreeDestroy(RadixTree *radix, RadixCBType func, void *cbctx);
RadixNode *RadixTreeLookup(RadixTree *radix, RadixPrefix *prefix);
void RadixTreeRemove(RadixTree *radix, RadixNode *node);
RadixNode *RadixTreeSearchExact(RadixTree *radix, RadixPrefix *prefix);
RadixNode *RadixTreeSearchBest(RadixTree *radix, RadixPrefix *prefix);
void RadixTreeProcess(RadixTree *radix, RadixCBType func, void *cbctx);
RadixPrefix *RadixPrefixPton(const char *string, long len, const char **errmsg);
RadixPrefix *RadixPrefixFromBlob(unsigned char *blob, int len, int prefixlen);
const char *RadixPrefixAddrNtop(RadixPrefix *prefix, char *buf, size_t len);
const char *RadixPrefixNtop(RadixPrefix *prefix, char *buf, size_t len);
/*************************************************************************/

/*************************************************************************/
/* SLOT QUEUE  */
/*************************************************************************/
typedef struct _SlotQueue
{

	struct
	{
		struct _DynBitMap *private;
		struct _DynBitMap *public;
	} busy_map;

	struct
	{
		int *arena;
		int index;
		int count;
	} slot;

	struct
	{
		unsigned int loaded:1;
		unsigned int mapped:1;
	} flags;

} SlotQueue;

void SlotQueueInit(SlotQueue *slot_queue, int slot_count, LibDataThreadSafeType type);
void SlotQueueMappedInit(SlotQueue *slot_queue, int slot_count, LibDataThreadSafeType type);
void SlotQueueReset(SlotQueue *slot_queue);
void SlotQueueDestroy(SlotQueue *slot_queue);
int SlotQueueMappedSetBusy(SlotQueue *slot_queue, int slot);
int SlotQueueMappedSetFree(SlotQueue *slot_queue, int slot);
int SlotQueueGrab(SlotQueue *slot_queue);
void SlotQueueFree(SlotQueue *slot_queue, int slot);
void SlotQueueDebugShow(SlotQueue *slot_queue);
/*************************************************************************/

/******************************************************************************************************/
#define MEMSLOT_CANARY_SEED			0x0B
#define MEMSLOT_DEFAULT_LIST_COUNT	8


typedef struct _MemSlotBase
{
	DLinkedList list[MEMSLOT_DEFAULT_LIST_COUNT];
	SlotQueue slots;
	MemArena *arena;
	pthread_mutex_t mutex;

	struct
	{
		pthread_mutex_t mutex;
		pthread_cond_t cond;
	} thrd_cond;

	struct
	{
		unsigned int loaded:1;
		unsigned int thread_safe:1;
	} flags;

} MemSlotBase;

typedef struct _MemSlotMetaData
{
	long canary00;

	DLinkedListNode node;
	long slot_id;
	int cur_list_id;

	struct
	{
		unsigned int foo:1;
	} flags;

	long canary01;
} MemSlotMetaData;

int MemSlotBaseInit(MemSlotBase *memslot_base, long slot_sz, long slot_count, LibDataThreadSafeType type);
int MemSlotBaseClean(MemSlotBase *memslot_base);
int MemSlotBaseLock(MemSlotBase *memslot_base);
int MemSlotBaseUnlock(MemSlotBase *memslot_base);
int MemSlotBaseIsEmptyList(MemSlotBase *memslot_base, int list_id);
int MemSlotBaseIsEmpty(MemSlotBase *memslot_base);
long MemSlotBaseSlotListSizeAll(MemSlotBase *memslot_base);
long MemSlotBaseSlotListIDSize(MemSlotBase *memslot_base, int list_id);
int MemSlotBaseSlotListIDSwitchToTail(MemSlotBase *memslot_base, int slot_id, int list_id);
int MemSlotBaseSlotListIDSwitch(MemSlotBase *memslot_base, int slot_id, int list_id);
void *MemSlotBaseSlotData(void *absolute_ptr);
void *MemSlotBaseSlotPointToHeadAndSwitchListID(MemSlotBase *memslot_base, int cur_list_id, int new_list_id);
void *MemSlotBaseSlotPointToHead(MemSlotBase *memslot_base, int list_id);
void *MemSlotBaseSlotPopHead(MemSlotBase *memslot_base, int list_id);
void *MemSlotBaseSlotPopTail(MemSlotBase *memslot_base, int list_id);
void *MemSlotBaseSlotGrabByID(MemSlotBase *memslot_base, int slot_id);
void *MemSlotBaseSlotGrabAndLeaveLocked(MemSlotBase *memslot_base);
void *MemSlotBaseSlotGrab(MemSlotBase *memslot_base);
int MemSlotBaseSlotGetID(void *slot_ptr);
int MemSlotBaseSlotFree(MemSlotBase *memslot_base, void *slot_ptr);
/******************************************************************************************************/



/**************************************************************************************************************************/
/* MAPPED MEMBUFFER STRUCTURES AND PROTOTYPES */
/**************************************************************************************************************************/
typedef struct _MemBufferMappedValidBytes
{
	long first_byte;
	long last_byte;
} MemBufferMappedValidBytes;

typedef struct _MemBufferMappedFlags
{
	unsigned int thread_safe:1;
	unsigned int wired:1;
	unsigned int no_core:1;
	unsigned int complete:1;
	unsigned int pct_string_update:1;
} MemBufferMappedFlags;

typedef struct _MemBufferMappedMetaData
{
	MemBufferMappedFlags flags;
	long cur_offset;
	int page_sz;
} MemBufferMappedMetaData;

typedef struct _MemBufferMapped
{
	DLinkedList partial_mb_list;
	MemBufferMappedMetaData metadata;
	MemBufferMappedFlags flags;
	pthread_mutex_t mutex;

	MemBuffer *main_mb;
	struct _DynBitMap *dyn_bitmap;
	float progress_pct;
	long cur_offset;
	int page_sz;

} MemBufferMapped;


MemBufferMapped *MemBufferMappedNew(LibDataThreadSafeType thrd_type, int page_size);
int MemBufferMappedInit(MemBufferMapped *mapped_mb, LibDataThreadSafeType thrd_type, int page_size);
int MemBufferMappedDestroy(MemBufferMapped *mapped_mb);
int MemBufferMappedClean(MemBufferMapped *mapped_mb);
long MemBufferMappedGetSize(MemBufferMapped *mapped_mb);
int MemBufferMappedPartialClean(MemBufferMapped *mapped_mb);
long MemBufferMappedPartialGetSize(MemBufferMapped *mapped_mb);
long MemBufferMappedMainGetSize(MemBufferMapped *mapped_mb);
int MemBufferMappedCompleted(MemBufferMapped *mapped_mb);
int MemBufferMappedAdd(MemBufferMapped *mapped_mb, char *data, long data_sz);
int MemBufferMappedWrite(MemBufferMapped *mapped_mb, long offset, long data_sz, char *data);
int MemBufferMappedCheckBytes(MemBufferMapped *mapped_mb, long base, long offset);
int MemBufferMappedGetValidBytes(MemBufferMapped *mapped_mb, long base, long offset, MemBufferMappedValidBytes *valid_bytes);
int MemBufferMappedCheckLastBlock(MemBufferMapped *mapped_mb, long total_sz);
float MemBufferMappedCalculateProgressPCT(MemBufferMapped *mapped_mb, long total_sz);
int MemBufferMappedGenerateStringPCT(MemBufferMapped *mapped_mb, char *ret_mask, int ret_mask_size, long total_sz);
MemBuffer *MemBufferMappedPartialGetByIdx(MemBufferMapped *mapped_mb, long block_idx);
int MemBufferMappedMetaDataPack(MemBufferMapped *mapped_mb, MetaData *dst_metadata, long item_sub_id);
int MemBufferMappedMetaDataUnPack(MemBufferMapped *mapped_mb, MemBuffer *raw_metadata_mb);
MemBuffer *MemBufferMappedMetaDataPackToMB(MemBufferMapped *mapped_mb, long item_sub_id);
MemBufferMapped *MemBufferMappedMetaDataUnPackFromMeta(MetaData *metadata);
MemBufferMapped *MemBufferMappedMetaDataUnPackFromMB(MemBuffer *raw_metadata_mb);

/**************************************************************************************************************************/
/* DYNAMIC BITMAP STRUCTURES AND PROTOTYPES */
/**************************************************************************************************************************/
typedef struct _DynBitMapMetaData
{
	long capacity;
	long grow_rate;
	long higher_seen;
	long bitset_count;
} DynBitMapMetaData;

typedef struct _DynBitMapValidBlockIdx
{
	long first_idx;
	long last_idx;
} DynBitMapValidBlockIdx;

typedef struct _DynBitMap
{
	DynBitMapMetaData metadata;
	pthread_mutex_t mutex;

	struct
	{
		char *data;
		long capacity;
		long grow_rate;
		long higher_seen;
		long bitset_count;
	} bitmap;

	struct
	{
		unsigned int thread_safe:1;
	} flags;

} DynBitMap;

DynBitMap *DynBitMapNew(LibDataThreadSafeType thrd_type, long grow_rate);
int DynBitMapInit(DynBitMap *dyn_bitmap, LibDataThreadSafeType thrd_type, long grow_rate);
int DynBitMapDestroy(DynBitMap *dyn_bitmap);
int DynBitMapClean(DynBitMap *dyn_bitmap);
int DynBitMapBitSet(DynBitMap *dyn_bitmap, long index);
int DynBitMapBitTest(DynBitMap *dyn_bitmap, long index);
int DynBitMapBitClear(DynBitMap *dyn_bitmap, long index);
int DynBitMapBitClearAll(DynBitMap *dyn_bitmap);
int DynBitMapCompare(DynBitMap *dyn_bitmap_one, DynBitMap *dyn_bitmap_two);
long DynBitMapGetHigherSeen(DynBitMap *dyn_bitmap);
int DynBitMapCheckMultiBlocks(DynBitMap *dyn_bitmap, long block_idx_begin, long block_idx_finish);
int DynBitMapGetValidBlocks(DynBitMap *dyn_bitmap, long block_idx_begin, long block_idx_finish, DynBitMapValidBlockIdx *valid_block_idx);
int DynBitMapGenerateStringMap(DynBitMap *dyn_bitmap, char *ret_mask, int ret_mask_size);

int DynBitMapMetaDataPack(DynBitMap *dyn_bitmap, MetaData *dst_metadata, long item_sub_id);
int DynBitMapMetaDataUnPack(DynBitMap *dyn_bitmap, MemBuffer *raw_metadata_mb);
MemBuffer *DynBitMapMetaDataPackToMB(DynBitMap *dyn_bitmap, long item_sub_id);
DynBitMap *DynBitMapMetaDataUnPackFromMeta(MetaData *metadata);
DynBitMap *DynBitMapMetaDataUnPackFromMB(MemBuffer *raw_metadata_mb);

/*************************************************************************/
/* IPV4_DB  */
/*************************************************************************/
typedef struct _IPv4TableConf
{
	int foo;
} IPv4TableConf;

typedef struct _IPv4Table
{
	RadixTree *tree;
	DLinkedList list;
} IPv4Table;

typedef struct _IPv4TableNode
{
	BrbDataDestroyCB *destroy_cb;
	DLinkedListNode node;
	struct in_addr addr;
	int ref_count;
	void *data;
} IPv4TableNode;

IPv4Table *IPv4TableNew(IPv4TableConf *conf);
int IPv4TableDestroy(IPv4Table *table);
IPv4TableNode *IPv4TableItemLookupByStrAddr(IPv4Table *table, char *addr_str);
IPv4TableNode *IPv4TableItemLookupByInAddr(IPv4Table *table, struct in_addr *addr);
IPv4TableNode *IPv4TableItemAddByStrAddr(IPv4Table *table, char *addr_str);
IPv4TableNode *IPv4TableItemAddByInAddr(IPv4Table *table, struct in_addr *addr);
int IPv4TableItemDelByStrAddr(IPv4Table *table, char *addr_str);
int IPv4TableItemDelByInAddr(IPv4Table *table, struct in_addr *addr);

/*************************************************************************/
/* SHA1  */
/*************************************************************************/

typedef struct {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t  buffer[64];
} BrbSha1Ctx;

#define BRB_SHA1_DIGEST_SIZE 20

void BrbSha1_Init(BrbSha1Ctx* context);
void BrbSha1_Update(BrbSha1Ctx* context, const uint8_t* data, const size_t len);
void BrbSha1_Final(BrbSha1Ctx* context, uint8_t digest[BRB_SHA1_DIGEST_SIZE]);
void BrbSha1_Transform(uint32_t state[5], const uint8_t buffer[64]);

/*************************************************************************/
/* Utils  */
/*************************************************************************/
typedef struct _BrbIpFamily
{
    sa_family_t family;

    union {
    	struct in_addr ip4;
		struct in6_addr ip6;
    } u;
} BrbIpFamily;

enum {
	IP_FAMILY_ALLOW_IPV4 = 1,
	IP_FAMILY_ALLOW_IPV6
};

int BrbHexToStr(char *hex, int hex_sz, char *dst_buf, int dst_buf_sz);
char *BrbHexToStrStatic(char *hex, int hex_sz);
int BrbStrReplaceAllNonAlpha(char *buf_str, int buf_sz);
char *BrbStrAddSlashes(char *src_str, int src_sz);
int BrbIsValidCpf(char *cpf_str);
int BrbIsValidCnpj(char *cnpj_str);
int BrbIsValidIpCidr(char *ip_cidr_str);
int BrbIsValidIp(char *ip_addr_str);
int BrbIsValidIpToSockAddr(char *ip_addr_str, struct sockaddr_storage *target_sockaddr);
int BrbIsValidIpV4(char *ip_addr_str, struct in_addr *ip4);
int BrbIpFamilyParse(const char *ip_str, BrbIpFamily *ip_family, unsigned char allow);
int BrbNetworkSockNtop(char *ret_data_ptr, int ret_maxlen, const struct sockaddr *sa, size_t salen);
int BrbNetworkSockMask(char *ret_data_ptr, int ret_maxlen, const struct sockaddr *sa);

int BrbIsNumeric(char *str);
int BrbIsHex(char *str);
int BrbIsDecimal(char *str);
int BrbStrSkipQuotes(char *buf_str, int buf_sz);
int BrbStrFindSubStr(char *buffer_str, int buffer_sz, char *substring_str, int substring_sz);
int BrbStrUrlDecode(char *str);
long BrbDateToTimestamp(char *date_str);
char *BrbStrSkipNoNumeric(char *buffer_str, int buffer_sz);
unsigned int BrbSimpleHashStrFmt(unsigned int seed, const char *key, ...);
unsigned int BrbSimpleHashStr(const char *key, unsigned int len, unsigned int seed);
unsigned char *BrbMacStrToOctedDup(char *mac_str);

#define BRB_COMPARE_NUM(a, b) (a > b) - (a < b)
#define SORT_COMPARE_NUM(x, y) (((x) > (y)) ? 1 : (((x) == (y)) ? 0 : 0))
int BrbStrCompare(char *strcur, char *strcmp);
/*************************************************************************/

#endif
