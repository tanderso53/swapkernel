/**
 * @file swapk.h
 * @author Tyler J. Anderson
 * @brief Swapkernel API header
 */

#ifndef SWAPK_H
#define SWAPK_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @defgroup swapk_api Swapkernel API
 * @{
 */

#ifndef SWAPK_SYSTEM_STACK_SIZE
#define SWAPK_SYSTEM_STACK_SIZE (4 * 1024)
#endif

#ifndef SWAPK_SYSTEM_PROC_PRIORITY
#define SWAPK_SYSTEM_PROC_PRIORITY 5
#endif

#ifndef SWAPK_ABSOLUTE_TIME_T

#include <time.h>
#define SWAPK_ABSOLUTE_TIME_T struct timespec

#ifndef SWAPK_FOREVER
#define SWAPK_FOREVER swapk_empty_time
#endif /* #ifndef SWAPK_FOREVER */

#endif /* #ifndef SWAPK_ABSOLUTE_TIME_T */

#define SWAPK_INVALID_PID ((swapk_pid_t)-1)

/* System events */
#define SWAPK_SYSTEM_EVENT_CONTEXT_SWITCH	0x00000001

/**
 * @defgroup swapk_proc Swapkernel Process API
 * @{
 */
#define SWAPK_DEFINE_STACK(name, stack_size)			\
	static uint8_t name ## _data[stack_size];		\
	static swapk_stack_t name = {				\
		.stacksize = stack_size,			\
		.stackbase = name ## _data,			\
		.stackptr = &name ## _data[stack_size - 1]	\
	};

extern struct timespec swapk_empty_time;

typedef uint16_t swapk_pid_t;
typedef void *(*swapk_entry)(void*);

typedef struct {
	void *stackptr;
	void *stackbase;
	unsigned int stacksize;
} swapk_stack_t;

typedef struct swapk_proc_node {
	swapk_stack_t *stack;
	swapk_entry entry;
	bool ready;
	int priority;
	swapk_pid_t pid;
	struct swapk_proc_node *prev;
	struct swapk_proc_node *next;
} swapk_proc_t;

/**
 * @}
 */ /* @defgroup swapk_proc Swapkernel Process API */

/**
 * @defgroup swapk_event Swapkernel Event API 
 * @{
 */

typedef struct {
	uint32_t active;
	bool fresh;
} swapk_event_t;

/**
 * @}
 */ /* @defgroup swapk_event Swapkernel Event API */

/**
 * @defgroup swapk_scheduler Scheduler API
 * @{
 */

typedef struct {
	void (*poll_event)(void*);
	void (*set_alarm)(SWAPK_ABSOLUTE_TIME_T, swapk_proc_t*);
} swapk_callbacks_t;

typedef struct {
	bool context_shift;
	swapk_proc_t *current;
	swapk_proc_t *procqueue;
	swapk_callbacks_t *cb_list;
	swapk_event_t events;
	uint16_t proc_cnt;

	/* Private members */
	swapk_proc_t _system_proc;
	uint8_t _system_stack_data[SWAPK_SYSTEM_STACK_SIZE];
	swapk_stack_t _system_stack;
} swapk_scheduler_t;

void swapk_scheduler_init(swapk_scheduler_t *sch,
			  swapk_callbacks_t *cb_list);

void swapk_scheduler_start(swapk_scheduler_t *sch);

void swapk_maybe_switch_context(swapk_scheduler_t *sch);

void swapk_idle_till_ready(swapk_scheduler_t *sch);

/** @brief Yield process without checking if preemptable */
void swapk_yield(swapk_scheduler_t *sch);

/** @brief Preempt a preemptable process and return to system */
void swapk_preempt(swapk_scheduler_t *sch);

/**
 * @}
 */ /* @defgroup swapk_scheduler Scheduler API */

/**
 * @addtogroup swapk_proc
 * @}
 */

void swapk_proc_init(swapk_scheduler_t *sch, swapk_proc_t *proc,
		     swapk_stack_t *stack, swapk_entry entry,
		     int priority);

swapk_proc_t *swapk_pop_proc(swapk_scheduler_t *sch);

swapk_proc_t *swapk_push_proc(swapk_scheduler_t *sch,
			      swapk_proc_t *proc);

swapk_proc_t *swapk_ready_proc(swapk_scheduler_t *sch,
			       swapk_proc_t *proc);

/**
 * @}
 */ /* @addtogroup swapk_proc */

/**
 * @defgroup swapk_wait_notify Wait/Notify System
 * @{
 */

void swapk_wait(swapk_scheduler_t *sch, SWAPK_ABSOLUTE_TIME_T time);

void swapk_wait_proc(swapk_scheduler_t *sch,
		     SWAPK_ABSOLUTE_TIME_T time,
		     swapk_proc_t *proc);

void swapk_wait_pid(swapk_scheduler_t *sch,
		    SWAPK_ABSOLUTE_TIME_T time,
		    swapk_pid_t pid);

void swapk_notify(swapk_scheduler_t *sch, swapk_proc_t *wake_up_proc);

void swapk_notify_pid(swapk_scheduler_t *sch, swapk_pid_t pid);

/**
 * @}
 */ /* @defgroup swapk_wait_notify Wait/Notify System */

/**
 * @addtogroup swapk_event
 * @{
 */

void swapk_event_init(swapk_event_t *event, uint32_t eventmask);

bool swapk_event_check(swapk_event_t *event, uint32_t eventmask);

void swapk_event_add(swapk_event_t *event, uint32_t eventmask);

void swapk_event_clear(swapk_event_t *event, uint32_t eventmask);

void swapk_event_clear_all(swapk_event_t *event);

/**
 * @}
 */ /* @addtogroup swapk_event */

/**
 * @}
 */ /* @defgroup swapk_api */

#endif /* #ifndef SWAPK_H */
