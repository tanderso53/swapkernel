#include "swapk-pico-integration.h"

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

static swapk_proc_t proca;
static swapk_proc_t procb;

static void *proca_entry(void*);
static void *procb_entry(void*);

void *proca_entry(void *arg)
{
	printf("We made it to proc A!\n");

	for (;;) {
		printf("Head of loop\n");
		sleep_ms(10000);
	}
}

void *procb_entry(void *arg)
{
	printf("We made it to proc B!\n");
	sleep_ms(18000);

	printf("Procb is ending!!!!\n");

	return arg;
}

int main()
{
	stdio_usb_init();

	while(!stdio_usb_connected()) {
		tight_loop_contents();
	}

	printf("You are connected!\n");

	swapk_pico_init();
	swapk_pico_proc_init(&proca, &stacka, proca_entry, 2);
	swapk_pico_proc_init(&procb, &stackb, procb_entry, 3);
	swapk_pico_start();
}
