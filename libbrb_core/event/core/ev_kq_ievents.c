/*
 * ev_kq_ievents.c
 *
 *  Created on: 2012-11-19
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

#include "../include/libbrb_ev_kq.h"

/**************************************************************************************************************************/
int EvKQBaseInternalEventSet(EvKQBase *kq_base, int ev_type, int action, EvBaseKQCBH *cb_handler, void *cb_data)
{
	/* Do not allow invalid INTERNAL EVENTS in this routine */
	if (ev_type >= KQ_BASE_INTERNAL_EVENT_LASTITEM)
		return 0;

	switch (action)
	{
	/******************************************************************/
	case COMM_ACTION_ADD_VOLATILE:
	{
		/* Set cb_handler and cb_data */
		kq_base->internal_ev[ev_type].cb_handler_ptr	= cb_handler;
		kq_base->internal_ev[ev_type].cb_data_ptr		= cb_data;

		/* Mark volatile and enable */
		kq_base->internal_ev[ev_type].flags.persist		= 0;
		kq_base->internal_ev[ev_type].flags.enabled		= 1;

		break;
	}
	/******************************************************************/
	case COMM_ACTION_ADD_PERSIST:
	{
		/* Set cb_handler and cb_data */
		kq_base->internal_ev[ev_type].cb_handler_ptr	= cb_handler;
		kq_base->internal_ev[ev_type].cb_data_ptr		= cb_data;

		/* Mark persist and enable */
		kq_base->internal_ev[ev_type].flags.persist		= 1;
		kq_base->internal_ev[ev_type].flags.enabled		= 1;

		break;
	}
	/******************************************************************/
	case COMM_ACTION_ENABLE:
	{
		if (!kq_base->internal_ev[ev_type].flags.enabled)
		{
			kq_base->internal_ev[ev_type].flags.enabled		= 1;
		}
		break;
	}
	/******************************************************************/
	case COMM_ACTION_DISABLE:
	{
		if (kq_base->internal_ev[ev_type].flags.enabled)
		{
			kq_base->internal_ev[ev_type].flags.enabled		= 0;
		}
		break;
	}
	/******************************************************************/
	case COMM_ACTION_DELETE:
	{
		if (kq_base->internal_ev[ev_type].flags.enabled)
		{
			/* Set cb_handler and cb_data */
			kq_base->internal_ev[ev_type].cb_handler_ptr	= NULL;
			kq_base->internal_ev[ev_type].cb_data_ptr		= NULL;

			/* Mark persist and enable */
			kq_base->internal_ev[ev_type].flags.persist		= 0;
			kq_base->internal_ev[ev_type].flags.enabled		= 0;
		}

		break;

	}
	/******************************************************************/
	}

	return 1;
}
/**************************************************************************************************************************/
int EvKQBaseInternalEventDispatch(EvKQBase *kq_base, int ev_data, int thrd_id, int ev_type)
{
	EvBaseKQCBH *cb_handler		= NULL;
	void *cb_handler_data		= NULL;

	/* Grab callback_ptr */
	cb_handler		= kq_base->internal_ev[ev_type].cb_handler_ptr;
	cb_handler_data	= kq_base->internal_ev[ev_type].cb_data_ptr;

	/* Touch time stamps */
	kq_base->internal_ev[ev_type].run.ts = kq_base->stats.cur_invoke_ts_sec;
	memcpy(&kq_base->internal_ev[ev_type].run.tv, &kq_base->stats.cur_invoke_tv, sizeof(struct timeval));

	/* There is a handler for this event. Invoke the damn thing */
	if (cb_handler)
		cb_handler(ev_data, ev_type, thrd_id, cb_handler_data, kq_base);

	return 1;
}
/**************************************************************************************************************************/
