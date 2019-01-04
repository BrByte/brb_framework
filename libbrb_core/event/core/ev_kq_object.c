/*
 * ev_kq_object.c
 *
 *  Created on: 2014-09-08
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
int EvKQBaseObjectDestroyAll(EvKQBase *kq_base)
{
	DLinkedListNode *node;
	EvBaseKQObject *kq_obj;
	int destroy_count;

	KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "Will unregister [%d] objects\n", kq_base->reg_obj.list.size);

	/* Walk all registered objects invoking object-specific DESTROY_CBH */
	while ((kq_obj = DLinkedListPopHead(&kq_base->reg_obj.list)))
	{
		BRB_ASSERT_FMT(kq_base, (kq_obj->flags.registered), "COUNT [%d] - OBJ [%p] - Not registered\n", destroy_count, kq_obj);

		/* Invoke destroy sequence */
		kq_obj->flags.destroy_invoked = 1;
		kq_obj->obj.destroy_cbh(kq_obj, kq_obj->obj.destroy_cbdata);
		destroy_count++;

		continue;
	}

	/* Reset registered object LIST */
	DLinkedListInit(&kq_base->reg_obj.list, BRBDATA_THREAD_UNSAFE);
	return destroy_count;
}
/**************************************************************************************************************************/
int EvKQBaseObjectRegister(EvKQBase *kq_base, EvBaseKQObject *kq_obj)
{
	/* Sanity check */
	if (!kq_obj)
		return 0;

	BRB_ASSERT_FMT(kq_base, (!kq_obj->flags.registered), "EVOBJ_TYPE [%p]-[%d] - Object already registered\n", kq_obj, kq_obj->code);

	/* Set KQ_OBJ data */
	kq_obj->kq_base				= kq_base;
	kq_obj->flags.registered	= 1;

	/* Register object */
	DLinkedListAdd(&kq_base->reg_obj.list, &kq_obj->node, kq_obj);

	return 1;
}
/**************************************************************************************************************************/
int EvKQBaseObjectUnregister(EvBaseKQObject *kq_obj)
{
	EvKQBase *kq_base;

	/* Sanity check */
	if (!kq_obj)
		return 0;

	/* Grab KQ from object */
	kq_base = kq_obj->kq_base;

	BRB_ASSERT_FMT(kq_base, (kq_base), "EVOBJ_TYPE [%p]-[%d] - Empty KQ_BASE\n", kq_obj, kq_obj->code);
	BRB_ASSERT_FMT(kq_base, (kq_obj->flags.registered), "EVOBJ_TYPE [%p]-[%d] - Object not registered\n", kq_obj, kq_obj->code);
	kq_obj->flags.registered = 0;

	/* Register object */
	DLinkedListDelete(&kq_base->reg_obj.list, &kq_obj->node);

	return 1;
}
/**************************************************************************************************************************/
