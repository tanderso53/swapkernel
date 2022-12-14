#ifndef SWAPK_PICO_LOCK_CORE_H
#define SWAPK_PICO_LOCK_CORE_H

/**
 * @addtogroup swapk_pico
 * @{
 */

/**
 * @defgroup swapk_pico_lock_core Pico Lock Core Integration
 * @{
 */

#define lock_owner_id_t uint16_t
#define LOCK_INVALID_OWNER_ID ((lock_owner_id_t)-1)

/**
 * @todo Uses private members. Need to add a library function to
 * swapk to access current PID
 */
#define lock_get_caller_owner_id()				\
	((lock_owner_id_t) (swapk_pico_get_current_pid()))

#define lock_is_owner_id_valid(id) ((id) != LOCK_INVALID_OWNER_ID)

#define lock_internal_spin_unlock_with_wait(lock, save)			\
	do {								\
		spin_unlock((lock)->spin_lock, save);			\
		swapk_pico_wait(at_the_end_of_time, lock, save);	\
	} while (0);

#define lock_internal_spin_unlock_with_notify(lock, save)	\
	do {							\
		spin_unlock((lock)->spin_lock, save);		\
		swapk_pico_notify(lock, save);			\
	} while (0);

#define lock_internal_spin_unlock_with_best_effort_wait_or_timeout(lock, save, until) ({ \
		spin_unlock((lock)->spin_lock, save);			\
		swapk_pico_wait(until, lock, save);			\
		})

#define sync_internal_yield_until_before(until)			\
	do {							\
		swapk_pico_yield_until(until);			\
	} while (0);

/**
 * @}
 */ /* @addtogroup swapk_pico */

/**
 * @}
 */ /* @defgroup swapk_pico_lock_core Pico Lock Core Integration */

#endif /* #ifndef SWAPK_PICO_LOCK_CORE_H */
