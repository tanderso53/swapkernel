/**
 * @file swapk.c
 * @author Tyler J. Anderson
 * @brief Swapkernel implementation source
 */

#include "swapk.h"

#include <stdlib.h>
#include <string.h>

/*
**********************************************************************
*                                                                    *
*                         Assembly Functions                         *
*                                                                    *
**********************************************************************
 */
extern void *_swapk_next;
extern void *_swapk_current;
extern void *scheduler_ptr;
extern void swapk_register_proc(void *entry, void *stack, void *end,
				void *arg);
extern void swapk_startup(void *systemsp, swapk_entry entry,
			  swapk_scheduler_t *sch);
extern void swapk_set_pending();
extern void swapk_svc_enable();
extern void swapk_svc_disable();
extern void swapk_svc_pend();

/*
**********************************************************************
*                                                                    *
*                      Internal API Functions                        *
*                                                                    *
**********************************************************************
*/

struct timespec swapk_empty_time = {0};
struct timespec swapk_full_time = {.tv_nsec = (long)-1, .tv_sec = (time_t)-1};

static int _swapk_proc_compare(swapk_proc_t *proca,
			       swapk_proc_t *procb);

static bool _swapk_is_proc_ready(swapk_scheduler_t *sch);

static void *_swapk_system_entry(void*);

static void *_swapk_sleep_entry(void*);

static void *_swapk_core_launch(void*);

static void _swapk_end_proc(void*);

static void _swapk_proc_swap(swapk_scheduler_t *sch,
			     swapk_proc_t *current,
			     swapk_proc_t *next);

static swapk_proc_t *_swapk_find_pid(swapk_scheduler_t *sch,
				     swapk_pid_t pid);

static void _swapk_svc_handler(swapk_scheduler_t *sch);

static void _swapk_call_common(swapk_scheduler_t *sch,
			       swapk_system_call call);

static int _swapk_call_scheduler_available(int argc, void **argv);

static bool _swapk_maybe_switch_context(swapk_scheduler_t *sch);

static void _swapk_wait_for_scheduler(swapk_scheduler_t *sch);

static bool _swapk_is_swapk_forever(SWAPK_ABSOLUTE_TIME_T time);

static bool _swapk_is_swapk_nowait(SWAPK_ABSOLUTE_TIME_T time);

static void _swapk_insert_sorted_forward(struct swapk_proc_queue *q,
					 swapk_proc_t *proc);

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
	proc->priority = priority;
	proc->pid = sch->proc_cnt++;
	proc->entry = entry;
	proc->core_affinity = 0;
	proc->core_id = -1;

	memset(proc->stack->stackbase, 0,
	       proc->stack->stacksize);

	swapk_register_proc(proc->entry, &proc->stack->stackptr,
			    _swapk_end_proc, sch);
	swapk_push_proc(sch, proc);
}

void swapk_scheduler_init(swapk_scheduler_t *sch,
			  swapk_callbacks_t *cb_list)
{
	sch->cb_list = cb_list;
	TAILQ_INIT(&sch->procqueue);
	sch->proc_cnt = 0;
	sch->cb_list->sem_sch_set_permits(1);

	for (SWAPK_CORE_ID_T i = 0; i < SWAPK_HARDWARE_THREADS; ++i) {
		sch->context_shift[i] = true;
		sch->current[i] = NULL;
		swapk_event_init(&sch->events[i], 0);

		/* Add the sleep processes */
		swapk_stack_t *sstack = &sch->_sleep_stack[i];
		sstack->stacksize = SWAPK_SLEEP_STACK_SIZE;
		sstack->stackbase = sch->_sleep_stack_data[i];
		sstack->stackptr
			= &sch->_sleep_stack_data[i][SWAPK_SLEEP_STACK_SIZE - 1];
		swapk_proc_init(sch, &sch->_sleep_proc[i],
				&sch->_sleep_stack[i],
				_swapk_sleep_entry,
				SWAPK_SLEEP_PROC_PRIORITY);
		sch->_sleep_proc[i].core_affinity = -1 * i;
	}

	/* Don't use library func to init system process, as we aren't
	 * starting it with pendsv, so we don't want to push a
	 * stack frame */
	swapk_proc_t *sys = &sch->_system_proc;
	sys->entry = _swapk_system_entry;
	sys->ready = true;
	sys->priority = SWAPK_SYSTEM_PROC_PRIORITY;
	sys->pid = sch->proc_cnt++;
	sys->stack = &sch->_system_stack;
	sys->stack->stacksize = SWAPK_SYSTEM_STACK_SIZE;
	sys->stack->stackbase = sch->_system_stack_data;
	sys->stack->stackptr
		= &sch->_system_stack_data[sys->stack->stacksize - 1];
	sys->core_affinity = 0;
	sys->core_id = -1;

	memset(sys->stack->stackbase, 0, sys->stack->stacksize);
}

void swapk_scheduler_start(swapk_scheduler_t *sch)
{
	if (sch->cb_list->core_launch)
		for (SWAPK_CORE_ID_T i = 1; i < SWAPK_HARDWARE_THREADS; ++i)
			sch->cb_list->core_launch(i, _swapk_core_launch,
						  (void*) sch);

	swapk_startup(sch->_system_proc.stack->stackptr,
		      sch->_system_proc.entry, sch);
}

swapk_proc_t *swapk_pop_proc(swapk_scheduler_t *sch)
{
	swapk_proc_t *ret;

	if (TAILQ_EMPTY(&sch->procqueue))
		return NULL;

	ret = TAILQ_FIRST(&sch->procqueue);
	TAILQ_REMOVE(&sch->procqueue, ret, _tailq_entry);

	return ret;
}

swapk_proc_t *swapk_push_proc(swapk_scheduler_t *sch,
			      swapk_proc_t *proc)
{
	_swapk_insert_sorted_forward(&sch->procqueue, proc);

	return proc;
}

swapk_proc_t *swapk_ready_proc(swapk_scheduler_t *sch,
			       swapk_proc_t *proc)
{
	if (!proc)
		return NULL;

	proc->ready = true;

	return proc;
}

swapk_pid_t swapk_proc_get_pid(swapk_scheduler_t *sch)
{
	swapk_proc_t *proc = swapk_proc_get(sch);

	return proc->pid;
}

swapk_proc_t *swapk_proc_get(swapk_scheduler_t *sch)
{
	SWAPK_CORE_ID_T cid = sch->cb_list->core_get_id();

	return sch->current[cid]
		? sch->current[cid]
		: &sch->_system_proc;
}

void swapk_wait(swapk_scheduler_t *sch, SWAPK_ABSOLUTE_TIME_T time)
{
	swapk_proc_t *proc;
	SWAPK_CORE_ID_T cid = sch->cb_list->core_get_id();

	/* Wait is expected to only be called by current proc */
	proc = sch->current[cid] ? sch->current[cid] : &sch->_system_proc;
	swapk_wait_proc(sch, time, proc);
}

void swapk_wait_proc(swapk_scheduler_t *sch, SWAPK_ABSOLUTE_TIME_T time,
		     swapk_proc_t *proc)
{
	proc->ready = false;

	if (_swapk_is_swapk_nowait(time))
		return;

	if (!_swapk_is_swapk_forever(time))
		sch->cb_list->set_alarm(time, proc);

	swapk_yield(sch);
}

void swapk_wait_pid(swapk_scheduler_t *sch, SWAPK_ABSOLUTE_TIME_T time,
		    swapk_pid_t pid)
{
	swapk_proc_t *proc = _swapk_find_pid(sch, pid);

	proc = proc ? proc : &sch->_system_proc;
	swapk_wait_proc(sch, time, proc);
}

void swapk_notify(swapk_scheduler_t *sch, swapk_proc_t *wake_up_proc)
{
	swapk_ready_proc(sch, wake_up_proc);
	swapk_preempt(sch);
}

void swapk_notify_pid(swapk_scheduler_t *sch, swapk_pid_t pid)
{
	swapk_proc_t *proc = _swapk_find_pid(sch, pid);
	swapk_notify(sch, proc);
}

void swapk_idle_till_ready(swapk_scheduler_t *sch)
{
	while (!_swapk_is_proc_ready(sch)) {
		sch->cb_list->poll_event(sch);
	}
}

void swapk_yield(swapk_scheduler_t *sch)
{
	SWAPK_CORE_ID_T cid = sch->cb_list->core_get_id();
	swapk_proc_t *current = sch->current[cid]
		? sch->current[cid]
		: &sch->_system_proc;

	swapk_proc_t *next = &sch->_system_proc;

	/* Swap to system thread and signal need for context shift */
	swapk_event_add(&sch->events[cid],
			SWAPK_SYSTEM_EVENT_CONTEXT_SWITCH);

	/* Do not allow a process to preempt itself */
	if (current == next)
		return;

	/* If use the blocking version, we will just keep calling
	 * swapk_yield() over and over again */
	while (!sch->cb_list->sem_sch_take_non_blocking())
		_swapk_wait_for_scheduler(sch);

	_swapk_proc_swap(sch, current, next);
}

void swapk_preempt(swapk_scheduler_t *sch)
{
	SWAPK_CORE_ID_T cid = sch->cb_list->core_get_id();
	swapk_proc_t *current = sch->current[cid]
		? sch->current[cid]
		: &sch->_system_proc;

	/* Do not preempt a process with priority less than 0 */
	if (current->priority < 0)
		return;

	swapk_yield(sch);
}

void swapk_call_scheduler_available(swapk_scheduler_t *sch)
{
	sch->_call_argc = 2;
	sch->_call_argv[0] = (void*) sch->_call;
	sch->_call_argv[1] = (void*) sch;
	sch->_call_argv[2] = NULL;
	_swapk_call_common(sch, _swapk_call_scheduler_available);
}

void swapk_event_init(swapk_event_t *event, uint32_t eventmask) {
	event->active = eventmask;
	event->fresh = false;
}

bool swapk_event_check(swapk_event_t *event, uint32_t eventmask)
{
	event->fresh = false;
	return event->active & eventmask;
}

void swapk_event_add(swapk_event_t *event, uint32_t eventmask)
{
	event->active |= eventmask;
	event->fresh = true;
}

void swapk_event_clear(swapk_event_t *event, uint32_t eventmask)
{
	event->active &= ~eventmask;
	event->fresh = false;
}

void swapk_event_clear_all(swapk_event_t *event)
{
	event->active = 0;
	event->fresh = false;
}

void swapk_scheduler_sort(swapk_scheduler_t *sch)
{
	/* Sort processes */
	swapk_proc_t *elem;
	swapk_proc_t *etemp;
	swapk_proc_t *tq_elem;
	swapk_proc_t *tq_temp;
	struct swapk_proc_queue *q = &sch->procqueue;
	struct swapk_proc_queue tq;

	/* Merge type sort with components pushed and sorted onto a
	 * new list */
	TAILQ_INIT(&tq);

	TAILQ_FOREACH_SAFE(elem, q, _tailq_entry, etemp) {
		TAILQ_REMOVE(q, elem, _tailq_entry);
		_swapk_insert_sorted_forward(&tq, elem);
	}

	/* Swap over the sorted queue to the scheduler queue */
	TAILQ_SWAP(&tq, q, swapk_proc_node, _tailq_entry);
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
	swapk_proc_t *elem;

	TAILQ_FOREACH(elem, &sch->procqueue, _tailq_entry)
		if (elem->ready) {
			return true;
		}

	return false;
}

void *_swapk_system_entry(void* arg)
{
	swapk_scheduler_t *sch = (swapk_scheduler_t*) arg;
	swapk_proc_t * proc = NULL;
	SWAPK_CORE_ID_T cid;

	sch->cb_list->sem_sch_take_blocking();

	for (;;) {
		cid = sch->cb_list->core_get_id();

		if ((proc = sch->current[cid])) {
			swapk_push_proc(sch, proc);
			sch->current[cid] = NULL;
		}

		if (_swapk_maybe_switch_context(sch)) {
			for (SWAPK_CORE_ID_T i = 0;
			     i < SWAPK_HARDWARE_THREADS; ++i) {
				swapk_event_clear(&sch->events[i],
						  SWAPK_SYSTEM_EVENT_SCH_AVAILABLE);
			}
		}
	}

	return arg;
}

void _swapk_end_proc(void *arg)
{
	swapk_scheduler_t *sch = (swapk_scheduler_t*) arg;
	SWAPK_CORE_ID_T cid = sch->cb_list->core_get_id();
	swapk_proc_t *current = sch->current[cid];
	swapk_proc_t *next;

	current->ready = false;
	next = &sch->_system_proc;
	sch->context_shift[cid] = true;
	sch->current[cid] = NULL;

	/* Switch to system proc */
	_swapk_proc_swap(sch, current, next);

	/* We shouldn't get here */
	for (;;) {
		swapk_yield(sch);
		sch->cb_list->poll_event(arg);
	}
}

void _swapk_proc_swap(swapk_scheduler_t *sch, swapk_proc_t *current,
		      swapk_proc_t *next)
{
	if (current == next)
		return;

	_swapk_current = &current->stack->stackptr;
	_swapk_next = &next->stack->stackptr;
	scheduler_ptr = sch;
	swapk_set_pending();
}

swapk_proc_t *_swapk_find_pid(swapk_scheduler_t *sch,
			      swapk_pid_t pid)
{
	swapk_proc_t *proc = NULL;
	SWAPK_CORE_ID_T cid = sch->cb_list->core_get_id();

	if (sch->current[cid] && sch->current[cid]->pid == pid) {
		proc = sch->current[cid];
	} else {
		swapk_proc_t *elem;

		TAILQ_FOREACH(elem, &sch->procqueue, _tailq_entry)
			if (elem->pid == pid) {
				proc = elem;
				break;
			}
	}

	return proc;
}

void *_swapk_core_launch(void* arg)
{
	swapk_scheduler_t *sch = (swapk_scheduler_t*) arg;
	SWAPK_CORE_ID_T cid = sch->cb_list->core_get_id();

	/* Extra hardware threads will start with a sleep task and
	 * will wake up when the scheduler tells them to */
	sch->current[cid] = &sch->_sleep_proc[cid];
	sch->current[cid]->core_id = cid;
	swapk_startup(sch->_sleep_stack->stackptr,
		      sch->_sleep_proc[cid].entry, sch);

	return arg;
}

void _swapk_svc_handler(swapk_scheduler_t *sch)
{
	if (sch->_call && !sch->_call_complete) {
		sch->_call_result = sch->_call(sch->_call_argc,
					       sch->_call_argv);
		sch->_call_complete = true;
	}

	swapk_svc_disable();
}

void isr_irq11()
{
	_swapk_svc_handler(scheduler_ptr);
}

void isr_irq8()
{
}

void isr_irq9()
{
}

void _swapk_call_common(swapk_scheduler_t *sch, swapk_system_call call)
{
	scheduler_ptr = sch;
	sch->_call_calling_pid = swapk_proc_get_pid(sch);
	sch->_call_complete = false;
	sch->_call_result = 0;
	sch->_call = call;
	swapk_svc_pend();
}

int _swapk_call_scheduler_available(int argc, void **argv)
{
	(void) argc;
	swapk_scheduler_t *sch = (swapk_scheduler_t*) argv[1];

	for (SWAPK_CORE_ID_T i = 0; i < SWAPK_HARDWARE_THREADS; ++i) {
		if (i != sch->cb_list->core_get_id()) {
			swapk_event_add(&sch->events[i],
					SWAPK_SYSTEM_EVENT_SCH_AVAILABLE);
		}
	}

	sch->cb_list->sem_sch_give();
	sch->cb_list->signal_event(sch);

	return 0;
}

int _swapk_scheduler_available(void *arg)
{
	swapk_scheduler_t *sch = (swapk_scheduler_t*) arg;

	for (SWAPK_CORE_ID_T i = 0; i < SWAPK_HARDWARE_THREADS; ++i) {
		if (i != sch->cb_list->core_get_id()) {
			swapk_event_add(&sch->events[i],
					SWAPK_SYSTEM_EVENT_SCH_AVAILABLE);
		}
	}

	sch->cb_list->sem_sch_give();
	sch->cb_list->signal_event(sch);

	return 0;
}

bool _swapk_maybe_switch_context(swapk_scheduler_t *sch)
{
	swapk_proc_t *current;
	swapk_proc_t *next;
	SWAPK_CORE_ID_T cid = sch->cb_list->core_get_id();

	/* Not all systems are using the event system yet, but the
	 * notify/wait functions do, so we need to check for an event
	 * AND check sch->context_shift */
	if (swapk_event_check(&sch->events[cid],
			      SWAPK_SYSTEM_EVENT_CONTEXT_SWITCH)) {
		sch->context_shift[cid] = true;
		swapk_event_clear(&sch->events[cid],
				  SWAPK_SYSTEM_EVENT_CONTEXT_SWITCH);
	}

	if (!sch->context_shift[cid])
		return false;

	current = sch->current[cid];

	if (current) {
		swapk_push_proc(sch, current);
	} else {
		current = &sch->_system_proc;
	}

	swapk_scheduler_sort(sch);

	next = swapk_pop_proc(sch);

	/* No longer need to handle switching to scheduler, as this
	 * func is only called from scheduler */
	if (next && next->ready && next->core_affinity != (-1 * (cid + 1))) {
		sch->current[cid] = next;
		sch->context_shift[cid] = false;
		swapk_call_scheduler_available(sch);
		_swapk_proc_swap(sch, current, next);

		return true;
	}

	swapk_push_proc(sch, next);

	return false;;
}

void *_swapk_sleep_entry(void* arg)
{
	swapk_scheduler_t *sch = (swapk_scheduler_t*) arg;

	for (;;) {
		sch->cb_list->poll_event(sch);
		SWAPK_CORE_ID_T cid = sch->cb_list->core_get_id();

		if (swapk_event_check(&sch->events[cid],
				      (SWAPK_SYSTEM_EVENT_SCH_AVAILABLE |
				       SWAPK_SYSTEM_EVENT_CONTEXT_SWITCH)))
			swapk_yield(sch);
	}
}

void _swapk_wait_for_scheduler(swapk_scheduler_t *sch)
{
	SWAPK_CORE_ID_T cid = sch->cb_list->core_get_id();

	while (swapk_event_check(&sch->events[cid],
				 SWAPK_SYSTEM_EVENT_SCH_AVAILABLE)) {
		sch->cb_list->poll_event(sch);
	}
}

bool _swapk_is_swapk_forever(SWAPK_ABSOLUTE_TIME_T time)
{
	return !memcmp(&time, &SWAPK_FOREVER, sizeof(struct timespec));
	/* return time.tv_nsec == SWAPK_FOREVER.tv_nsec && */
	/* 	time.tv_sec == SWAPK_FOREVER.tv_sec; */
}

bool _swapk_is_swapk_nowait(SWAPK_ABSOLUTE_TIME_T time)
{
	return !memcmp(&time, &SWAPK_NOWAIT, sizeof(struct timespec));
	/* return time.tv_nsec == SWAPK_NOWAIT.tv_nsec && */
	/* 	time.tv_sec == SWAPK_NOWAIT.tv_sec; */
}

void _swapk_insert_sorted_forward(struct swapk_proc_queue *q,
				  swapk_proc_t *proc)
{
	swapk_proc_t *elem, *tempelem;

	/* Check if we are first */
	if (TAILQ_EMPTY(q)) {
		TAILQ_INSERT_TAIL(q, proc, _tailq_entry);

		return;
	}

	/* Make sure we aren't already in the list */
	TAILQ_FOREACH(elem, q, _tailq_entry)
		if (elem == proc)
			return;

	TAILQ_FOREACH_SAFE(elem, q, _tailq_entry, tempelem) {
		if (_swapk_proc_compare(elem, proc) < 0) {
			TAILQ_INSERT_BEFORE(elem, proc, _tailq_entry);
			break;
		}

		/* Need this or we will lose elements */
		if (!TAILQ_NEXT(elem, _tailq_entry))
			TAILQ_INSERT_TAIL(q, proc, _tailq_entry);
	}
}
