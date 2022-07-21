/**
 * @file swapk.h
 * @author Tyler J. Anderson
 * @brief Swapkernel API header
 */

#ifndef SWAPK_H
#define SWAPK_H

#include <stdint.h>
#include <stdbool.h>

#include "queue.h"

/**
 * @defgroup swapk_api Swapkernel API
 * @{
 */

#ifndef SWAPK_SYSTEM_STACK_SIZE
#define SWAPK_SYSTEM_STACK_SIZE (4 * 1024)
#endif

#ifndef SWAPK_SLEEP_STACK_SIZE
#define SWAPK_SLEEP_STACK_SIZE (4 * 1024)
#endif

#ifndef SWAPK_SYSTEM_PROC_PRIORITY
#define SWAPK_SYSTEM_PROC_PRIORITY -1
#endif

#ifndef SWAPK_SLEEP_PROC_PRIORITY
#define SWAPK_SLEEP_PROC_PRIORITY 20
#endif

#ifndef SWAPK_HARDWARE_THREADS
#define SWAPK_HARDWARE_THREADS 1
#endif

#ifndef SWAPK_UNMANAGED_PROCS
/** @brief Set greater than 1 to enable unmanaged processes
 *
 * Unmanaged processes are processes that are swapped using another
 * manager (i.e. not Swapkernel). It can be helpful to inform
 * swapkernel about these processes so that Swapkernel won't lock out
 * resources belonging to that system.
 *
 * There are no functions that utilize unmanaged processes in the
 * swapkernel core; Swapkernel only supplies storage for them. If this
 * feature is used, it will be up to the implementer to initialize and
 * enqueue each individual process, and to add exceptions for spinlock
 * and synchronization primatives.
 *
 * @note Unmanaged processes are an experimental feature and is not
 * fully supported by swapkernel. Expect changes.
 */
#define SWAPK_UNMANAGED_PROCS 0
#endif

#ifndef SWAPK_ABSOLUTE_TIME_T

#include <time.h>
#define SWAPK_ABSOLUTE_TIME_T struct timespec

#ifndef SWAPK_FOREVER
#define SWAPK_FOREVER swapk_full_time
#endif /* #ifndef SWAPK_FOREVER */

#ifndef SWAPK_NOWAIT
#define SWAPK_NOWAIT swapk_empty_time
#endif /* #ifndef SWAPK_NOWAIT */

#endif /* #ifndef SWAPK_ABSOLUTE_TIME_T */

#ifndef SWAPK_CORE_ID_T
#define SWAPK_CORE_ID_T uint8_t
#endif

#define SWAPK_INVALID_PID ((swapk_pid_t)-1)

/* System events */
#define SWAPK_SYSTEM_EVENT_CONTEXT_SWITCH	0x00000001
#define SWAPK_SYSTEM_EVENT_SCH_AVAILABLE	0x00000002
#define SWAPK_SYSTEM_EVENT_PREEMPT_DISABLED	0x00000004

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
extern struct timespec swapk_full_time;

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

	/**
	 * <0: Always operate on -1 * (core_id + 1)
	 * 0: Operate on any core (Default)
	 * >0: Always operate on core_id + 1
	 */
	int core_affinity;

	/**
	 * The core_id process is running on. <0 if
	 * process is not running on any core
	 */
	int core_id;

	TAILQ_ENTRY(swapk_proc_node) _tailq_entry;
} swapk_proc_t;

TAILQ_HEAD(swapk_proc_queue, swapk_proc_node);

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

typedef int (*swapk_system_call)(int argc, void **argv);

typedef struct {
	void (*poll_event)(void*);
	void (*signal_event)(void*);
	void (*set_alarm)(SWAPK_ABSOLUTE_TIME_T, swapk_proc_t*);

	/* If NULL, will be ignored */
	void (*core_launch)(SWAPK_CORE_ID_T, swapk_entry, void*);
	SWAPK_CORE_ID_T (*core_get_id)();

	/* Not sure if this is needed, the scheduler semaphore may
	 * solve this problem. The idea is to prevent simultaneous
	 * pushes and pops of the task queue, but all these pushes
	 * and pops should probably be moved to the scheduler. */
	void (*mutex_lock_queue)(void);
	void (*mutex_unlock_queue)(void);

	/**
	 * Sch can only be on one core at a time. Semaphore required
	 * to ensure this. In the mean time the core will switch to
	 * the sleep job
	 */
	void (*sem_sch_set_permits)(int);
	bool (*sem_sch_take_non_blocking)();
	void (*sem_sch_take_blocking)();
	void (*sem_sch_give)();
} swapk_callbacks_t;

typedef struct {
	bool context_shift[SWAPK_HARDWARE_THREADS];
	swapk_proc_t *current[SWAPK_HARDWARE_THREADS];
	struct swapk_proc_queue procqueue;
#if SWAPK_UNMANAGED_PROCS > 0
	struct swapk_proc_queue unmanaged_queue;
	swapk_proc_t *unmanaged;
	swapk_proc_t _unmanaged[SWAPK_UNMANAGED_PROCS];
#endif /* #if SWAPK_UNMANAGED_PROCS > 0 */
	swapk_callbacks_t *cb_list;
	swapk_event_t events[SWAPK_HARDWARE_THREADS];
	uint16_t proc_cnt;

	/* Private members */
	swapk_proc_t _system_proc;
	uint8_t _system_stack_data[SWAPK_SYSTEM_STACK_SIZE];
	swapk_stack_t _system_stack;
	swapk_proc_t _sleep_proc[SWAPK_HARDWARE_THREADS];
	uint8_t _sleep_stack_data[SWAPK_HARDWARE_THREADS][SWAPK_SLEEP_STACK_SIZE];
	swapk_stack_t _sleep_stack[SWAPK_HARDWARE_THREADS];
	swapk_system_call _call;
	int _call_argc;
	void *_call_argv[32];
	int _call_result;
	bool _call_complete;
	pid_t _call_calling_pid;
	swapk_proc_t *_current[SWAPK_HARDWARE_THREADS];
	swapk_proc_t *_next[SWAPK_HARDWARE_THREADS];
	swapk_proc_t *_last[SWAPK_HARDWARE_THREADS];
} swapk_scheduler_t;

void swapk_scheduler_init(swapk_scheduler_t *sch,
			  swapk_callbacks_t *cb_list);

void swapk_scheduler_start(swapk_scheduler_t *sch);

void swapk_idle_till_ready(swapk_scheduler_t *sch);

/** @brief Yield process without checking if preemptable */
void swapk_yield(swapk_scheduler_t *sch);

/** @brief Preempt a preemptable process and return to system */
void swapk_preempt(swapk_scheduler_t *sch);

void swapk_call_scheduler_available(swapk_scheduler_t *sch);

/** @brief Sort processes in the scheduler's queue */
void swapk_scheduler_sort(swapk_scheduler_t *sch);

/**
 * @}
 */ /* @defgroup swapk_scheduler Scheduler API */

/**
 * @addtogroup swapk_proc
 * @{
 */

void swapk_proc_init(swapk_scheduler_t *sch, swapk_proc_t *proc,
		     swapk_stack_t *stack, swapk_entry entry,
		     int priority);

swapk_proc_t *swapk_pop_proc(swapk_scheduler_t *sch);

swapk_proc_t *swapk_push_proc(swapk_scheduler_t *sch,
			      swapk_proc_t *proc);

swapk_proc_t *swapk_ready_proc(swapk_scheduler_t *sch,
			       swapk_proc_t *proc);

swapk_pid_t swapk_proc_get_pid(swapk_scheduler_t *sch);

swapk_proc_t *swapk_proc_get(swapk_scheduler_t *sch);

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
