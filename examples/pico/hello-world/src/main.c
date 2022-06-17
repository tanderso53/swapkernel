#include "swapk-pico-integration.h"

#include "pico/stdlib.h"
#include "pico/sync.h"
#include "hardware/sync.h"
#include "hardware/exception.h"

#include <stdio.h>
#include <string.h>

#define SWAPK_STACK_SIZE_SETUP 1024
#define SWAPK_STACK_SIZE_A (4 * 2048)
#define SWAPK_STACK_SIZE_B (4 * 2048)

SWAPK_DEFINE_STACK(stacksetup, SWAPK_STACK_SIZE_SETUP);
SWAPK_DEFINE_STACK(stacka, SWAPK_STACK_SIZE_A);
SWAPK_DEFINE_STACK(stackb, SWAPK_STACK_SIZE_B);

static swapk_proc_t procsetup;
static swapk_proc_t proca;
static swapk_proc_t procb;
static semaphore_t app_setup_sem;

static void *setup_entry(void*);
static void *proca_entry(void*);
static void *procb_entry(void*);

void *setup_entry(void *arg)
{
	stdio_usb_init();

	while(!stdio_usb_connected()) {
		tight_loop_contents();
	}

	sem_release(&app_setup_sem);
	printf("You are connected!\n");

	return arg;
}

void *proca_entry(void *arg)
{
	sem_acquire_blocking(&app_setup_sem);
	sem_release(&app_setup_sem);

	for (;;) {
		printf("Hello World from Proc A on core %d!\n",
		       swapk_pico_scheduler()->cb_list->core_get_id());
		sleep_ms(10000);
	}
}

void *procb_entry(void *arg)
{
	int cnt = 5;

	sem_acquire_blocking(&app_setup_sem);
	sem_release(&app_setup_sem);

	while(cnt-- > 0) {
		printf("Hello World from Proc B on core %d!\n",
		       swapk_pico_scheduler()->cb_list->core_get_id());
		sleep_ms(18000);
	}

	printf("Proc B is ending on core %d!!!!\n",
	       swapk_pico_scheduler()->cb_list->core_get_id());

	return arg;
}

int main()
{
	sem_init(&app_setup_sem, 0, 1); /* For setup functions in proca */
	swapk_pico_init();
	swapk_pico_proc_init(&procsetup, &stacksetup, setup_entry, -10);
	swapk_pico_proc_init(&proca, &stacka, proca_entry, 2);
	swapk_pico_proc_init(&procb, &stackb, procb_entry, 3);
	swapk_pico_start();
}
