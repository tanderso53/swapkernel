#ifndef SWAPK_H
#define SWAPK_H

#include <stdint.h>
#include <stdbool.h>

#ifndef SWAPK_SYSTEM_STACK_SIZE
#define SWAPK_SYSTEM_STACK_SIZE (4 * 1024)
#endif

#ifndef SWAPK_SYSTEM_PROC_PRIORITY
#define SWAPK_SYSTEM_PROC_PRIORITY 5
#endif

#define SWAPK_DEFINE_STACK(name, stack_size)			\
	static uint8_t name ## _data[stack_size];		\
	static swapk_stack_t name = {				\
		.stacksize = stack_size,			\
		.stackbase = name ## _data,			\
		.stackptr = &name ## _data[stack_size - 1]	\
	};

typedef void *(*swapk_entry)(void*);
typedef void (*swapk_poll_irq)(void*);

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
	struct swapk_proc_node *prev;
	struct swapk_proc_node *next;
} swapk_proc_t;

typedef struct {
	bool context_shift;
	swapk_proc_t *current;
	swapk_proc_t *procqueue;
	swapk_poll_irq poll_irq;

	/* Private members */
	swapk_proc_t _system_proc;
	uint8_t _system_stack_data[SWAPK_SYSTEM_STACK_SIZE];
	swapk_stack_t _system_stack;
} swapk_scheduler_t;

void swapk_proc_init(swapk_scheduler_t *sch, swapk_proc_t *proc,
		     swapk_stack_t *stack, swapk_entry entry,
		     int priority);

void swapk_scheduler_init(swapk_scheduler_t *sch,
			  swapk_poll_irq pollirq_cb);

void swapk_scheduler_start(swapk_scheduler_t *sch);

void swapk_maybe_switch_context(swapk_scheduler_t *sch);

swapk_proc_t *swapk_pop_proc(swapk_scheduler_t *sch);

swapk_proc_t *swapk_push_proc(swapk_scheduler_t *sch,
			      swapk_proc_t *proc);

swapk_proc_t *swapk_ready_proc(swapk_scheduler_t *sch,
			       swapk_proc_t *proc);

void swapk_idle_till_ready(swapk_scheduler_t *sch);

/** @brief Preempt a preemptable process and return to system */
void swapk_preempt(swapk_scheduler_t *sch);

#endif /* #ifndef SWAPK_H */
