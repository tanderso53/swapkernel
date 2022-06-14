/**
 * @file swapk_pico_integration.c
 * @author Tyler J. Anderson
 * @brief Example integration of swapkernel into the Pico SDK
 */

#include "swapk-pico-integration.h"
#include "pico/multicore.h"

#include "string.h"

#ifndef ARRAY_LEN
#define ARRAY_LEN(array) (sizeof(array)/sizeof(array[0]))
#endif

typedef struct swapk_pico_lock_map_node {
	lock_core_t lock_core;
	swapk_pid_t pid;
	bool used;
	TAILQ_ENTRY(swapk_pico_lock_map_node) _tailq;
} swapk_pico_lock_map_t;
	
static swapk_pico_lock_map_t _swapk_pico_lock_map_data[SWAPK_PICO_LOCK_MAP_LENGTH];
static TAILQ_HEAD(_swapk_pico_lock_queue, swapk_pico_lock_map_node)
	_swapk_pico_lock_maps, _swapk_pico_lock_maps_free;
static swapk_scheduler_t _swapk_pico_scheduler;
static swapk_callbacks_t _swapk_pico_cbs;
static unsigned int _swapk_pico_lock_map_cntr;
static alarm_pool_t *_swapk_pico_alarm_pool;
static swapk_entry _swapk_pico_core1_entry;
static void *_swapk_pico_core1_arg;
static mutex_t _swapk_pico_mtx;
static semaphore_t _swapk_pico_sch_sem;

static struct timespec _swapk_pico_get_timespec(absolute_time_t time);
static absolute_time_t _swapk_pico_get_absolute_time(struct timespec time);

static void _swapk_pico_poll_event(void *arg);
static void _swapk_pico_set_alarm(SWAPK_ABSOLUTE_TIME_T time,
				  swapk_proc_t *proc);
static swapk_pico_lock_map_t *_swapk_pico_find_free_lock_map();
static swapk_pico_lock_map_t
	*_swapk_pico_free_lock_map(swapk_pico_lock_map_t *lock_map);
static void _swapk_pico_cb_signal_event(void* arg);
static void _swapk_pico_entry_wrapper();
static void _swapk_pico_cb_core_launch(SWAPK_CORE_ID_T cid,
				       swapk_entry entry, void* arg);
static SWAPK_CORE_ID_T _swapk_pico_cb_core_get_id();
static void _swapk_pico_cb_mutex_lock_queue();
static void _swapk_pico_cb_mutex_unlock_queue();
static void _swapk_pico_cb_sem_sch_set_permits(int permits);
static bool _swapk_pico_cb_sem_sch_take_non_blocking();
static void _swapk_pico_cb_sem_sch_take_blocking();
static void _swapk_pico_cb_sem_sch_give();

swapk_scheduler_t *swapk_pico_scheduler()
{
	return &_swapk_pico_scheduler;
}

void swapk_pico_init() {
	swapk_callbacks_t *cbs = &_swapk_pico_cbs;

	cbs->poll_event = _swapk_pico_poll_event;
	cbs->set_alarm = _swapk_pico_set_alarm;
	cbs->core_get_id = _swapk_pico_cb_core_get_id;
	cbs->core_launch = _swapk_pico_cb_core_launch;
	cbs->mutex_lock_queue = _swapk_pico_cb_mutex_lock_queue;
	cbs->mutex_unlock_queue = _swapk_pico_cb_mutex_unlock_queue;
	cbs->sem_sch_give = _swapk_pico_cb_sem_sch_give;
	cbs->sem_sch_set_permits = _swapk_pico_cb_sem_sch_set_permits;
	cbs->sem_sch_take_blocking = _swapk_pico_cb_sem_sch_take_blocking;
	cbs->sem_sch_take_non_blocking = _swapk_pico_cb_sem_sch_take_non_blocking;
	cbs->signal_event = _swapk_pico_cb_signal_event;

	_swapk_pico_lock_map_cntr = 0;

	/* Add all lock maps to the free queue so we can find them
	 * when we need them */
	for (int i = 0; i < ARRAY_LEN(_swapk_pico_lock_map_data); ++i) {
		swapk_pico_lock_map_t *elem = &_swapk_pico_lock_map_data[i];
		elem->used = false;
		TAILQ_INSERT_TAIL(&_swapk_pico_lock_maps_free,
				  elem, _tailq);
	}

	swapk_scheduler_init(&_swapk_pico_scheduler, &_swapk_pico_cbs);

	/* Use a separate alarm pool for swapkernel */
	_swapk_pico_alarm_pool = alarm_pool_create(SWAPK_PICO_HARDWARE_ALARM_NO,
						   SWAPK_PICO_MAX_TIMERS);
}

void swapk_pico_start() {
	swapk_scheduler_start(&_swapk_pico_scheduler);
}

void swapk_pico_proc_init(swapk_proc_t *proc, swapk_stack_t *stack,
			  swapk_entry entry, int priority)
{
	swapk_proc_init(&_swapk_pico_scheduler, proc, stack, entry,
			priority);
}

void swapk_pico_wait(absolute_time_t time,
		     lock_core_t *lock_core, uint32_t save)
{
	SWAPK_CORE_ID_T cid = _swapk_pico_scheduler.cb_list->core_get_id();
	SWAPK_ABSOLUTE_TIME_T ts = _swapk_pico_get_timespec(time);

	/** @todo Should expose the internal swapk API func that does
	 * this */
	if (memcmp(&ts, &SWAPK_NOWAIT, sizeof(SWAPK_ABSOLUTE_TIME_T))) {
		swapk_pico_lock_map_t *lock_map;

		lock_map = _swapk_pico_find_free_lock_map();

		if (!lock_map)
			panic("Swapk_pico no lock maps available!!!\n");

		lock_map->pid = _swapk_pico_scheduler.current[cid]->pid;
		lock_map->lock_core = *lock_core;
		spin_unlock(lock_core->spin_lock, save);
	}

	swapk_wait(&_swapk_pico_scheduler, ts);
}

void swapk_pico_yield_until(absolute_time_t time)
{
	swapk_wait(&_swapk_pico_scheduler,
		   _swapk_pico_get_timespec(time));

	/* This is where we are returning from the scheduler, so make sure
	 * everything is popped off the stack from the assembly functions
	 * before returning */
	__dsb();
	__isb();
}

void swapk_pico_notify(lock_core_t *lock_core, uint32_t save)
{
	swapk_pico_lock_map_t *lock_map = NULL;
	swapk_pico_lock_map_t *tlm = NULL;
	struct _swapk_pico_lock_queue *lq = &_swapk_pico_lock_maps;
	pid_t pid = 0;

	TAILQ_FOREACH_SAFE(lock_map, lq, _tailq, tlm) {
		/* Free the matching lock map */
		if (lock_map &&
		    lock_map->lock_core.spin_lock == lock_core->spin_lock
		    && lock_map->used) {
			pid = lock_map->pid;
			_swapk_pico_free_lock_map(lock_map);

			break;
		}
	}

	spin_unlock(lock_core->spin_lock, save);

	/* Don't preempt if the lock was never registered in the first
	 * place */
	if (!lock_map)
		return;

	__sev();
	swapk_notify_pid(&_swapk_pico_scheduler, pid);
}

lock_owner_id_t swapk_pico_get_current_pid()
{
	SWAPK_CORE_ID_T cid = _swapk_pico_scheduler.cb_list->core_get_id();

	return _swapk_pico_scheduler.current[cid]
		? _swapk_pico_scheduler.current[cid]->pid
		: _swapk_pico_scheduler._system_proc.pid;
}

swapk_pico_lock_map_t *_swapk_pico_find_free_lock_map()
{
	swapk_pico_lock_map_t *lock_map;

	lock_map = TAILQ_FIRST(&_swapk_pico_lock_maps_free);

	/* Will be NULL if no free ones available, so let's not operate
	 * on the NULL ones */
	if (lock_map) {
		/* Mark this one as taken */
		TAILQ_REMOVE(&_swapk_pico_lock_maps_free, lock_map,
			     _tailq);
		TAILQ_INSERT_TAIL(&_swapk_pico_lock_maps, lock_map,
				  _tailq);
		lock_map->used = true;
	}

	return lock_map;
}

swapk_pico_lock_map_t *_swapk_pico_free_lock_map(swapk_pico_lock_map_t *lock_map)
{
	swapk_pico_lock_map_t *elem;
	struct _swapk_pico_lock_queue *used, *free;

	used = &_swapk_pico_lock_maps;
	free = &_swapk_pico_lock_maps_free;

	/* First check that the map is used */
	TAILQ_FOREACH(elem, used, _tailq)
		if (elem == lock_map)
			return NULL; /* NULL if not added to free */

	/* Remove from used and add to end of free */
	TAILQ_REMOVE(used, lock_map, _tailq);
	TAILQ_INSERT_TAIL(free, lock_map, _tailq);
	lock_map->used = false;

	return lock_map;
}

void _swapk_pico_poll_event(void *arg)
{
	__wfe();
}

int64_t _swapk_pico_alarm_handler(alarm_id_t id, void *user_data)
{
	swapk_notify(&_swapk_pico_scheduler, (swapk_proc_t*) user_data);

	return 0;
}

void _swapk_pico_set_alarm(SWAPK_ABSOLUTE_TIME_T time,
			   swapk_proc_t *proc)
{
	alarm_pool_add_alarm_at(_swapk_pico_alarm_pool,
				_swapk_pico_get_absolute_time(time),
				_swapk_pico_alarm_handler,
				(void*) proc, true);
}

static struct timespec _swapk_pico_get_timespec(absolute_time_t time)
{
	struct timespec ts;
	uint64_t us = to_us_since_boot(time);

	if (us < to_us_since_boot(get_absolute_time()))
	    us = 0;

	ts.tv_sec = us / 1000000;
	ts.tv_nsec = (us % 1000000) * 1000;

	return ts;
}

static absolute_time_t _swapk_pico_get_absolute_time(struct timespec time)
{
	absolute_time_t at;
	uint64_t us = (time.tv_sec * 1000000) + (time.tv_nsec / 1000);

	update_us_since_boot(&at, us);

	return at;
}

void _swapk_pico_cb_signal_event(void* arg)
{
	__sev();
}

void _swapk_pico_entry_wrapper()
{
	_swapk_pico_core1_entry(_swapk_pico_core1_arg);
}

void _swapk_pico_cb_core_launch(SWAPK_CORE_ID_T cid,
				swapk_entry entry, void* arg)
{
	switch (cid) {
	case 0:
		entry(arg);
		break;
	case 1:
		_swapk_pico_core1_entry = entry;
		_swapk_pico_core1_arg = arg;
		/* May need to use the raw version and pass a custom
		 * stack pointer */
		multicore_launch_core1(_swapk_pico_entry_wrapper);
		break;
	default:
		panic("Attempted to launch invalid core %d\n", cid);
		break;
	}
}

SWAPK_CORE_ID_T _swapk_pico_cb_core_get_id()
{
	return (SWAPK_CORE_ID_T) get_core_num();
}

void _swapk_pico_cb_mutex_lock_queue()
{
	mutex_enter_blocking(&_swapk_pico_mtx);
}

void _swapk_pico_cb_mutex_unlock_queue()
{
	mutex_exit(&_swapk_pico_mtx);
}

void _swapk_pico_cb_sem_sch_set_permits(int permits)
{
	sem_init(&_swapk_pico_sch_sem, permits, permits);
}

bool _swapk_pico_cb_sem_sch_take_non_blocking()
{
	return (sem_acquire_timeout_ms(&_swapk_pico_sch_sem, 0));
}

void _swapk_pico_cb_sem_sch_take_blocking()
{
	sem_acquire_blocking(&_swapk_pico_sch_sem);
}

void _swapk_pico_cb_sem_sch_give()
{
	sem_release(&_swapk_pico_sch_sem);
}

