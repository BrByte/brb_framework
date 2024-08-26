/*
 * libbrb_core.h
 *
 *  Created on: 2014-09-24
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

#ifndef LIBBRB_CORE_H_
#define LIBBRB_CORE_H_
/**********************************************************************************************************************/
/* BRB_CORE core defines */
#define WITNESS_MT_LOCKS			0
#define WITNESS_MT_LOCKS_FD			stdout
#ifndef NULL
#define NULL (void*)0
#endif
/**********************************************************************************************************************/
/* MUTEX PRIMITIVES
/**********************************************************************************************************************/
#if WITNESS_MT_BRBDATA
#define MUTEX_INIT(mutex, mutex_name)	fprintf (WITNESS_MT_BRBDATA_FD, "WITNESS [THRD: %X] - [%s]-[%s]-[INIT] -> ([%s] line [%d])\n",\
		pthread_self(), __func__, mutex_name, __FILE__, __LINE__); \
		pthread_mutex_init((pthread_mutex_t*)(&mutex), 0);

#define MUTEX_LOCK(mutex, mutex_name)	fprintf (WITNESS_MT_BRBDATA_FD, "WITNESS [THRD: %X] - [%s]-[%s]-[LOCK] -> ([%s] line [%d])\n",\
		pthread_self(), __func__, mutex_name, __FILE__, __LINE__); \
		pthread_mutex_lock((pthread_mutex_t*)(&mutex));

#define MUTEX_TRYLOCK(mutex, mutex_name, state)	fprintf (WITNESS_MT_BRBDATA_FD, "WITNESS [THRD: %X] - [%s]-[%s]-[TRYLOCK] -> ([%s] line [%d])\n",\
		pthread_self(), __func__, mutex_name, __FILE__, __LINE__); \
		state = pthread_mutex_trylock((pthread_mutex_t*)(&mutex));

#define MUTEX_UNLOCK(mutex, mutex_name)	fprintf (WITNESS_MT_BRBDATA_FD, "WITNESS [THRD: %X] - [%s]-[%s]-[UNLOCK] -> ([%s] line [%d])\n",\
		pthread_self(), __func__, mutex_name, __FILE__, __LINE__); \
		pthread_mutex_unlock((pthread_mutex_t*)(&mutex));

#define MUTEX_DESTROY(mutex, mutex_name) fprintf (WITNESS_MT_BRBDATA_FD, "WITNESS [THRD: %X] - [%s]-[%s]-[DESTROY] -> ([%s] line [%d])\n",\
		pthread_self(), __func__, mutex_name, __FILE__, __LINE__); \
		pthread_mutex_destroy((pthread_mutex_t*)(&mutex));

#else
#define MUTEX_INIT(mutex, mutex_name)			pthread_mutex_init( (pthread_mutex_t*)(&mutex), 0);
#define MUTEX_LOCK(mutex, mutex_name)			pthread_mutex_lock( (pthread_mutex_t*)(&mutex));
#define MUTEX_TRYLOCK(mutex, mutex_name, state)	state = pthread_mutex_trylock( (pthread_mutex_t*)(&mutex));
#define MUTEX_UNLOCK(mutex, mutex_name)			pthread_mutex_unlock( (pthread_mutex_t*)(&mutex));
#define MUTEX_DESTROY(mutex, mutex_name)		pthread_mutex_destroy((pthread_mutex_t*)(&mutex));
#endif
/**********************************************************************************************************************/
/* BIT-OPERATION PRIMITIVES
/**********************************************************************************************************************/
#define EBIT_SET(flag, bit)     ((void)((flag) |= ((1L<<(bit)))))
#define EBIT_CLR(flag, bit)     ((void)((flag) &= ~((1L<<(bit)))))
#define EBIT_TEST(flag, bit)	((flag) & ((1L<<(bit))))

#define BITMASK_BIT(bit)           (1<<((bit)%8))
#define BITMASK_BIN(mask, bit)     (mask)[(bit)>>3]
#define BITMASK_SET(mask, bit)     ((void)(BITMASK_BIN(mask, bit) |= BITMASK_BIT(bit)))
#define BITMASK_CLR(mask, bit)     ((void)(BITMASK_BIN(mask, bit) &= ~BITMASK_BIT(bit)))
#define BITMASK_TEST(mask, bit)    ((BITMASK_BIN(mask, bit) & BITMASK_BIT(bit)) != 0)

#define BRB_ASSERT(ev_base, condition, msg)				if (!condition) EvKQBaseAssert(ev_base, __func__, __FILE__, __LINE__, msg, NULL)
#define BRB_ASSERT_FMT(ev_base, condition, msg, ...)	if (!condition) EvKQBaseAssert(ev_base, __func__, __FILE__, __LINE__, msg, __VA_ARGS__)
/**********************************************************************************************************************/
/* BRB_CORE global core variables */
/**********************************************************************************************************************/
#include "libbrb_data.h"
#include "libbrb_ev_kq.h"
#include "libbrb_ev_utils.h"
/**********************************************************************************************************************/
#endif /* LIBBRB_CORE_H_ */
