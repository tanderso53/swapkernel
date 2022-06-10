/**
 * @file swapk-pico-integration.h
 * @author Tyler J. Anderson
 * @brief Pico SDK functions for swapkernel projects
 */

#ifndef SWAPK_PICO_INTEGRATION_H
#define SWAPK_PICO_INTEGRATION_H

#define SWAPK_PICO_LOCK_MAP_LENGTH 256

#include "pico/time.h"
#include "pico/lock_core.h"

#include "swapk.h"

/**
 * @defgroup swapk_pico Pico functions for Swapkernel
 * @{
 */

swapk_scheduler_t *swapk_pico_scheduler();

void swapk_pico_init();

void swapk_pico_start();

void swapk_pico_proc_init(swapk_proc_t *proc, swapk_stack_t *stack,
			  swapk_entry entry, int priority);

void swapk_pico_wait(absolute_time_t time,
		     lock_core_t *lock_core, uint32_t save);

void swapk_pico_notify(lock_core_t *lock_core, uint32_t save);

/**
 * @}
 */

#endif /* #ifndef SWAPK_PICO_INTEGRATION_H */
