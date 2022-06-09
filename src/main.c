#include "swapk.h"

#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "hardware/exception.h"
/* #include "core_cm0plus.h" */
/* #include "RP2040.h" */

#include <stdio.h>
#include <string.h>

#define SWAPK_KERNEL_STACK_SIZE (4*1024)
#define SWAPK_STACK_SIZE_A (4 * 128)
#define SWAPK_STACK_SIZE_B (4 * 128)

SWAPK_DEFINE_STACK(stacka, SWAPK_STACK_SIZE_A);
SWAPK_DEFINE_STACK(stackb, SWAPK_STACK_SIZE_B);

static swapk_scheduler_t sch;
static swapk_proc_t proca;
static swapk_proc_t procb;
static swapk_proc_t *proclist[2];
static uint8_t stackk[SWAPK_KERNEL_STACK_SIZE];

static void kernel_init();
static int64_t alarm_handler(alarm_id_t id, void *user_data);
static void *proca_entry(void*);
static void *procb_entry(void*);
static void handle_pendsv();
static void wait_poll_irq();
void swapk_sleep_ms(uint32_t ms);

void wait_poll_irq(void* arg)
{
	__wfi();
}

void kernel_init()
{
	swapk_scheduler_init(&sch, wait_poll_irq);
	swapk_proc_init(&sch, &proca, &stacka, proca_entry, 2);
	swapk_proc_init(&sch, &procb, &stackb, procb_entry, 2);

	/* This seems necessary, even if the handler is not used */
	exception_set_exclusive_handler(PENDSV_EXCEPTION, handle_pendsv);

	alarm_pool_init_default();
	swapk_scheduler_start(&sch);
}

void swapk_sleep_ms(uint32_t ms)
{
	sch.current->ready = false;
	add_alarm_in_ms(ms, alarm_handler, sch.current, true);
	sch.context_shift = true;
	swapk_preempt(&sch);
}

int64_t alarm_handler(alarm_id_t id, void *user_data)
{
	swapk_proc_t *proc = (swapk_proc_t*) user_data;
	swapk_ready_proc(&sch, proc);
	swapk_preempt(&sch);

	return 0;
}

void *proca_entry(void *arg)
{
	printf("We made it to proc A!\n");

	for (;;) {
		printf("Head of loop\n");
		swapk_sleep_ms(10000);
	}
}

void *procb_entry(void *arg)
{
	printf("We made it to proc B!\n");
	swapk_sleep_ms(18000);

	printf("Procb is ending!!!!\n");

	return arg;
}

void handle_pendsv()
{
	sch.context_shift = true;
}

int main()
{
	stdio_usb_init();

	while(!stdio_usb_connected()) {
		tight_loop_contents();
	}

	printf("You are connected!\n");

	kernel_init();
}
