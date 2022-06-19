#ifndef _LIBAM_THREAD_POOL_H_
#define _LIBAM_THREAD_POOL_H_

#include <stdint.h>

#include <libam/libam_types.h>
#include <libam/libam_time.h>
#include <libam/libam_stats.h>

typedef void* (*lam_thread_func_t)(void* arg);

typedef	enum  lam_thread_pool_flags {
	LIBAM_THREAD_POOL_NONE			= 0 << 0,
	LIBAM_THREAD_POOL_BLOCKING		= 1 << 0, /* Use locks & block. Better idle CPU usage & lower task execution latencies.
	 	 	 	 	 	 	 	 	 	 	 	 However, this also adds some latency to all tasks, especially durin high-concurrenly. */
	LIBAM_THREAD_POOL_LAZY_START	= 1 << 1, /* Do not immediately start min_threads, wait until first tasks are scheduled */
	LIBAM_THREAD_POOL_FUNC_OVERRIDE	= 1 << 2, /* Allow specification of custom functions when default function is set */
} lam_thread_pool_flags_t;

typedef struct lam_thread_pool_config {
	lam_thread_pool_flags_t flags; /* See libam_thread_pool_flags_t */
	lam_thread_func_t default_func; /* Default thread function to execute */
	amtime_t	poll_freq;		/* Time, in microseconds, a thread will suspend for before checking new work. Set 0 for default value. */
	amtime_t	idle_timeout;	/* Time, in microseconds, a thread will remain idle before halting. 0 for never shutting down idle threads. */
	uint64_t	max_threads;	/* Maximum number of concurrent threads to have running. 0 to have no cap */
	uint64_t	min_threads;	/* Number of threads that always must be running at any given time. Set 0 for default value. */
	uint64_t	backlog;		/* Max depth of task queue. Set 0 for default value. */
} lam_thread_pool_config_t;

typedef struct lam_thread_pool_stats {
	uint64_t		threads_created;	/* Total number of threads created over the lifetime of the pool */
	uint64_t		tasks_created;	/* Total number of tasks scheduled over the lifetime of the pool */

	amstat_range_t	active_thread_count; /* Total thread count at time of scheduling of a task */
	amstat_range_t	idle_thread_count; /* Idle thread count at time of scheduling of a task (Subset of active) */
	amstat_range_t	task_delay;	/* Time, in microseconds, tasks spent queued */
	amstat_range_t	tasks_processed;	/* Number of tasks thread have processed before idle_timeout expired. */
	amstat_range_t	busy_task_num;	/* Number of tasks threads process before becoming idle */
	amstat_range_t	queue_depth;	/* Number of tassks in queue at the time of scheduling */
} lam_thread_pool_stats_t;

struct lam_thread_pool;
typedef struct lam_thread_pool lam_thread_pool_t;

lam_thread_pool_t* lam_thread_pool_create(const lam_thread_pool_config_t* config);
amrc_t lam_thread_pool_destroy(lam_thread_pool_t* tp, lam_thread_pool_stats_t* stats);

/* Setters & getters */
uint64_t lam_thread_pool_get_thread_count(const lam_thread_pool_t* tp);
uint64_t lam_thread_pool_get_idle_thread_count(const lam_thread_pool_t* tp);
uint64_t lam_thread_pool_get_min_thread_count(const lam_thread_pool_t* tp);
uint64_t lam_thread_pool_get_max_thread_count(const lam_thread_pool_t* tp);

amrc_t lam_thread_pool_set_default_func(lam_thread_pool_t* tp, lam_thread_func_t value);
amrc_t lam_thread_pool_set_idle_timeout(lam_thread_pool_t* tp, amtime_t value);
amrc_t lam_thread_pool_set_min_thread_count(lam_thread_pool_t* tp, uint64_t value);
amrc_t lam_thread_pool_set_max_thread_count(lam_thread_pool_t* tp, uint64_t value);

/* Queue a task to execute via the thread pool
 * @Returns AMRC_SUCCESS / AMRC_ERROR. Nothing is queued when errors happen */
amrc_t lam_thread_pool_run(lam_thread_pool_t* tp, lam_thread_func_t func, void* arg, void** ret_ptr);

#endif /* _LIBAM_THREAD_POOL_H_ */
