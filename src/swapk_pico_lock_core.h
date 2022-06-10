#ifndef SWAPK_PICO_LOCK_CORE_H
#define SWAPK_PICO_LOCK_CORE_H

#define lock_owner_id_t uint16_t
#define LOCK_INVALID_OWNER_ID ((lock_owner_id_t)-1)

/**
 * @todo Uses private members. Need to add a library function to
 * swapk to access current PID
 */
/* #define lock_get_caller_owner_id()					\ */
/* 	((lock_owner_id_t) (swapk_pico_scheduler()->current		\ */
/* 			    ? swapk_pico_scheduler()->current->pid	\ */
/* 			    : swapk_pico_scheduler()->_system_proc.pid)) */

#define lock_is_owner_id_valid(id) ((id) != LOCK_INVALID_OWNER_ID)

#define lock_internal_spin_unlock_with_wait(lock, save)		\
	do {							\
		spin_unlock((lock)->spin_lock, save);		\
		swapk_pico_wait(nil_time, lock, save);		\
	} while (0);

#define lock_internal_spin_unlock_with_notify(lock, save)	\
	do {							\
		spin_unlock((lock)->spin_lock, save);		\
		__sev();					\
		swapk_pico_notify(lock, save);			\
	} while (0);

#define lock_internal_spin_unlock_with_best_effort_wait_or_timeout(lock, save, until) ({ \
		spin_unlock((lock)->spin_lock, save);			\
		swapk_pico_wait(until, lock, save);			\
		})

#define sync_internal_yield_until_before(until)			\
	do {							\
		swapk_wait(swapk_pico_scheduler(), until);	\
	} while (0);

#endif /* #ifndef SWAPK_PICO_LOCK_CORE_H */
