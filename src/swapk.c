#include "swapk.h"

#include <stdlib.h>
#include <string.h>

/* From assembly files */
extern void *_swapk_next;
extern void *_swapk_current;
extern void swapk_register_proc(void *entry, void *stack, void *end,
				void *arg);
extern void swapk_startup(void *systemsp, swapk_entry entry,
			  swapk_scheduler_t *sch);
extern void swapk_set_pending();

/*
**********************************************************************
*                                                                    *
*                  Internal API Functions                            *
*                                                                    *
**********************************************************************
*/

static int _swapk_proc_compare(swapk_proc_t *proca,
			       swapk_proc_t *procb);

static bool _swapk_is_proc_ready(swapk_scheduler_t *sch);

static void *_swapk_system_entry(void*);

static void _swapk_end_proc(void*);

static void _swapk_proc_swap(swapk_scheduler_t *sch,
			     swapk_proc_t *current,
			     swapk_proc_t *next);

/*
**********************************************************************
*                                                                    *
*                      API Implementation                            *
*                                                                    *
**********************************************************************
*/

void swapk_proc_init(swapk_scheduler_t *sch, swapk_proc_t *proc,
		     swapk_stack_t *stack, swapk_entry entry,
		     int priority)
{
	proc->stack = stack;
	proc->ready = true;
	proc->prev = NULL;
	proc->next = NULL;
	proc->priority = priority;
	proc->pid = sch->proc_cnt++;
	proc->entry = entry;

	memset(proc->stack->stackbase, 0,
	       proc->stack->stacksize);

	swapk_register_proc(proc->entry, &proc->stack->stackptr,
			    _swapk_end_proc, sch);
	swapk_push_proc(sch, proc);
}

void swapk_scheduler_init(swapk_scheduler_t *sch,
			  swapk_poll_irq pollirq_cb)
{
	sch->context_shift = true;
	sch->poll_irq = pollirq_cb;
	sch->current = NULL;
	sch->procqueue = NULL;
	sch->proc_cnt = 0;

	/* Don't use library func to init system process, as we aren't
	 * starting it with pendsv, so we don't want to push a
	 * stack frame */
	swapk_proc_t *sys = &sch->_system_proc;
	sys->entry = _swapk_system_entry;
	sys->ready = true;
	sys->next = NULL;
	sys->prev = NULL;
	sys->priority = SWAPK_SYSTEM_PROC_PRIORITY;
	sys->pid = sch->proc_cnt++;
	sys->stack = &sch->_system_stack;
	sys->stack->stacksize = SWAPK_SYSTEM_STACK_SIZE;
	sys->stack->stackbase = sch->_system_stack_data;
	sys->stack->stackptr
		= &sch->_system_stack_data[sys->stack->stacksize - 1];

	memset(sys->stack->stackbase, 0, sys->stack->stacksize);
}

void swapk_scheduler_start(swapk_scheduler_t *sch)
{
	swapk_startup(sch->_system_proc.stack->stackptr,
		      sch->_system_proc.entry, sch);
}

void swapk_maybe_switch_context(swapk_scheduler_t *sch)
{
	swapk_proc_t *current;
	swapk_proc_t *next;

	/* This is the maybe part */
	if (!sch->context_shift)
		return;

	current = sch->current;
	if (current) {
		swapk_push_proc(sch, current);
	} else {
		current = &sch->_system_proc;
	}
	swapk_idle_till_ready(sch);
	next = swapk_pop_proc(sch);

	if (next) {
		while (!next->ready) {
			swapk_proc_t *prev = next;
			/* We shouldn't get null because idle_till
			 * ready returned */
			next = swapk_pop_proc(sch);
			swapk_push_proc(sch, prev);
		}

		sch->current = next;
		sch->context_shift = false;
	} else {
		next = &sch->_system_proc;
		sch->current = NULL;
		/* Need to switch context again if going back to
		 * system */
		sch->context_shift = true;
	}

	_swapk_proc_swap(sch, current, next);
}

swapk_proc_t *swapk_pop_proc(swapk_scheduler_t *sch)
{
	swapk_proc_t *ret;

	if (!(ret = sch->procqueue))
		return NULL;

	sch->procqueue = sch->procqueue->next;

	if (sch->procqueue)
		sch->procqueue->prev = NULL;

	return ret;
}

swapk_proc_t *swapk_push_proc(swapk_scheduler_t *sch,
			      swapk_proc_t *proc)
{
	swapk_proc_t *elem = NULL;
	swapk_proc_t *nextelem = sch->procqueue;

	/* Check if we are first */
	if (!nextelem) {
		sch->procqueue = proc;
		proc->next = NULL;
		proc->prev = NULL;

		return proc;
	}

	/* Make sure we aren't already in the list */
	while (nextelem) {
		if (nextelem == proc)
			return proc;

		elem = nextelem;
		nextelem = elem->next;
	}

	elem = NULL;
	nextelem = sch->procqueue;

	while (nextelem) {
		if (_swapk_proc_compare(nextelem, proc) < 0)
			break;

		elem = nextelem;
		nextelem = elem->next;
	}

	/* Check if we are last */
	if (!nextelem) {
		elem->next = proc;
		proc->prev = elem;
		proc->next = NULL;

		return proc;
	}

	/* Guess we are in the middle */
	elem->next = proc;
	proc->prev = elem;
	proc->next = nextelem;
	nextelem->prev = proc;

	return proc;
}

swapk_proc_t *swapk_ready_proc(swapk_scheduler_t *sch,
			       swapk_proc_t *proc) {
	swapk_proc_t *elem = NULL;
	swapk_proc_t *prevelem = proc->prev;

	proc->ready = true;

	/* Don't need to reorder if we are already the current proc */
	if (!prevelem)
		return proc;

	while (prevelem && _swapk_proc_compare(prevelem, proc) > 0) {
		elem = prevelem;
		prevelem = elem->prev;
	}

	/* Looks like we are up next! */
	if (!prevelem) {
		proc->next = elem;
		elem->prev = proc;
		sch->procqueue = proc;
	} else {
		proc->next = prevelem;
		proc->prev = prevelem->prev;
		prevelem->prev->next = proc;
		prevelem->prev = proc;
	}

	return proc;
}

void swapk_idle_till_ready(swapk_scheduler_t *sch)
{
	while (!_swapk_is_proc_ready(sch)) {
		sch->poll_irq(NULL);
	}
}

void swapk_preempt(swapk_scheduler_t *sch)
{
	swapk_proc_t *current = sch->current
		? sch->current
		: &sch->_system_proc;

	swapk_proc_t *next = &sch->_system_proc;

	/* Do not preempt a process with priority less than 0 */
	if (current->priority < 0)
		return;

	if (sch->current) {
		swapk_push_proc(sch, sch->current);
		sch->current = NULL;
	}

	/* Check if order has changed */
	/* The swapk_ready_proc() should do this, but let's make this
	 * an option if we want to additional assurances everything is
	 * correct */
#ifdef SWAPK_EXTRA_SCHEDULER_CHECKS
	swapk_proc_t *elem = NULL;
	swapk_proc_t *nextelem = sch->procqueue;
	while (nextelem) {
		elem = nextelem;
		nextelem = elem->next;

		if (_swapk_proc_compare(elem, nextelem) < 0) {
			nextelem->prev = elem->prev;
			elem->next = nextelem->next;
			nextelem->next = elem;
			elem->prev = nextelem;
			elem = nextelem;
			nextelem = elem->next;

			if (!elem->prev) {
				sch->procqueue = elem;
			}
		}
	}
#endif /* #ifdef SWAPK_EXTRA_SCHEDULER_CHECKS */

	/* Swap to system thread and signal need for context shift */
	sch->context_shift = true;

	/* Do not allow a process to preempt itself */
	if (current == next)
		return;

	_swapk_proc_swap(sch, current, next);
}

/*
**********************************************************************
*                                                                    *
*                    Internal API Implementation                     *
*                                                                    *
**********************************************************************
*/

int _swapk_proc_compare(swapk_proc_t *proca, swapk_proc_t *procb)
{
	/* If procb belongs right of proca, > 1, 0 if belong in same
	 * place, < 1 if proca belongs right of procb */
	if (!procb) {
		return 1;
	}

	if (!proca) {
		return -1;
	}

	if (proca->priority == procb->priority
	    && proca->ready == procb->ready) {
		return 0;
	}

	if (proca->ready == procb->ready) {
		return proca->priority < procb->priority ? 1 : -1;
	}

	return proca->ready ? 1 : -1;
}

bool _swapk_is_proc_ready(swapk_scheduler_t *sch)
{
	swapk_proc_t *next = sch->procqueue;

	while (next) {
		if (next->ready) {
			return true;
		}

		next = next->next;
	}

	return false;
}

void *_swapk_system_entry(void* arg)
{
	swapk_scheduler_t *sch = (swapk_scheduler_t*) arg;

	for (;;) {
		if (!sch->procqueue)
			break;

		swapk_maybe_switch_context(sch);
	}

	return arg;
}

void _swapk_end_proc(void *arg)
{
	swapk_scheduler_t *sch = (swapk_scheduler_t*) arg;
	swapk_proc_t *current = sch->current;
	swapk_proc_t *next;

	current->ready = false;
	next = &sch->_system_proc;
	sch->context_shift = true;
	sch->current = NULL;

	/* Switch to system proc */
	_swapk_proc_swap(sch, current, next);

	/* We shouldn't get here */
	for (;;) {
		sch->poll_irq(NULL);
	}
}

void _swapk_proc_swap(swapk_scheduler_t *sch, swapk_proc_t *current,
		      swapk_proc_t *next)
{
	if (current == next)
		return;

	_swapk_current = &current->stack->stackptr;
	_swapk_next = &next->stack->stackptr;
	swapk_set_pending();
}
