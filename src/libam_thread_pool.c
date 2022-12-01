#include <string.h>
#include <stdlib.h>
#include <pthread.h>


#include "libam/libam_thread_pool.h"

#include "libam/libam_stack.h"
#include "libam/libam_atomic.h"

#ifdef DEBUG
#include "libam/libam_log.h"
#define DEBUG_MASK (1UL << 62)
#define debug_log(fmt, args...)	amlog_sink_log(AMLOG_DEBUG, DEBUG_MASK, fmt, ##args)
#define error_log(fmt, args...)	amlog_sink_log(AMLOG_ERROR, DEBUG_MASK, fmt, ##args)
#else
#define debug_log(fmt, args...)
#define error_log(fmt, args...)
#endif

struct lam_thread_pool {
	uint64_t id;
	lam_thread_pool_config_t config;

	amstack_t *tasks_queue;

	volatile uint64_t threads_created;
	volatile uint64_t threads_destroyed;
	volatile uint64_t tasks_created;
	volatile uint64_t active_thread_count;
	volatile uint64_t idle_thread_count;
	volatile uint64_t running_id;
	volatile uint64_t drain_signal;

	lam_thread_pool_stats_t stats;
	pthread_mutex_t stats_mutex;
};

typedef struct lam_thread_pool_task {
	uint64_t id;
	lam_thread_func_t func;
	void *arg;
	void **ret_ptr;

	/* Stat-related numbers */
	amtime_t queue_time;
	uint64_t active_thread_count;
	uint64_t idle_thread_count;
	uint64_t queue_depth;
} lam_thread_pool_task_t;


static volatile uint64_t thread_pool_index = 1;

static void lam_thread_pool_stats_init(lam_thread_pool_stats_t* stats)
{
	stats->threads_created = 0;
	stats->tasks_created = 0;

	amstat_init(&stats->active_thread_count);
	amstat_init(&stats->idle_thread_count);
	amstat_init(&stats->task_delay);
	amstat_init(&stats->tasks_processed);
	amstat_init(&stats->busy_task_num);
	amstat_init(&stats->queue_depth);
}

static void lam_thread_pool_stats_fold(lam_thread_pool_t *tp, lam_thread_pool_stats_t *stats)
{
	pthread_mutex_lock(&tp->stats_mutex);

	tp->stats.threads_created += stats->threads_created;
	tp->stats.tasks_created += stats->tasks_created;

	amstat_add(&tp->stats.active_thread_count, &stats->active_thread_count);
	amstat_add(&tp->stats.idle_thread_count, &stats->idle_thread_count);
	amstat_add(&tp->stats.task_delay, &stats->task_delay);
	amstat_add(&tp->stats.tasks_processed, &stats->tasks_processed);
	amstat_add(&tp->stats.busy_task_num, &stats->busy_task_num);
	amstat_add(&tp->stats.queue_depth, &stats->queue_depth);

	pthread_mutex_unlock(&tp->stats_mutex);
}

static inline ambool_t lam_thread_pool_should_stop(lam_thread_pool_t *tp, uint64_t id, amtime_t now, amtime_t last_work)
{
	if (tp->config.idle_timeout == 0 || (now - last_work < tp->config.idle_timeout))
		return am_false;

	if (id <= tp->config.min_threads)
		return am_false;

	return am_true;
}

static void* lam_thread_pool_worker_func(void *arg)
{
	lam_thread_pool_t *tp = arg;
	uint64_t busy_tasks_processed = 0;
	uint64_t total_tasks_processed = 0;
	struct timespec poll_timeout = { .tv_sec = 0, .tv_nsec = 0 };
	uint64_t thread_id = amsync_inc(&tp->running_id);
	lam_thread_pool_task_t *task;
	amtime_t now;
	amtime_t last_work;
	amrc_t rc;
	void *ret;
	lam_thread_pool_stats_t local_stats;

	amsync_inc(&tp->active_thread_count);
	amsync_inc(&tp->idle_thread_count);

	lam_thread_pool_stats_init(&local_stats);

	debug_log("tp worker %lu-%lu started\n", tp->id, thread_id);

	now = amtime_now();
	last_work = now;
	while (1) {
		rc = amstack_pop(tp->tasks_queue, (void**) &task);
		if (rc != AMRC_SUCCESS) {
			/* Thread is idle */
			if (busy_tasks_processed > 0) {
				amstat_upd(&local_stats.busy_task_num, busy_tasks_processed);
				total_tasks_processed += busy_tasks_processed;
				busy_tasks_processed = 0;
				amsync_inc(&tp->idle_thread_count);
			}

			if (tp->drain_signal) {
				debug_log("tp worker %lu-%lu detected drain signal, stopping\n", tp->id, thread_id);
				break;
			}

			if (lam_thread_pool_should_stop(tp, thread_id, now, last_work)) {
				/* tp->config.idle_timeout expired, and conditions are met for this thread to stop */
				amstat_upd(&local_stats.tasks_processed, total_tasks_processed);
				debug_log("tp worker %lu-%lu inactive for %6.3lf seconds, stopping\n", tp->id, thread_id, ((double)(now - last_work)) / AMTIME_SEC);
				break;
			}

			/* Suspend for idle duration */
			poll_timeout.tv_nsec = tp->config.poll_freq;
			nanosleep(&poll_timeout, NULL);
			now = amtime_now();
			continue;
		}

		/* Got a task to process */

		debug_log("tp worker %lu-%lu dequeued task %lu-%lu\n", tp->id, thread_id, tp->id, task->id);
		if (busy_tasks_processed == 0)
			amsync_dec(&tp->idle_thread_count);
		now = amtime_now();
		amstat_upd(&local_stats.task_delay, now - task->queue_time);
		amstat_upd(&local_stats.active_thread_count, task->active_thread_count);
		amstat_upd(&local_stats.idle_thread_count, task->idle_thread_count);
		amstat_upd(&local_stats.queue_depth, task->queue_depth);
		busy_tasks_processed++;

		ret = task->func(task->arg);
		if (task->ret_ptr != NULL)
			*task->ret_ptr = ret;
		debug_log("tp worker %lu-%lu done processing %lu-%lu\n", tp->id, thread_id, tp->id, task->id);
		free(task);

		now = amtime_now();
		last_work = now;
	}

	lam_thread_pool_stats_fold(tp, &local_stats);

	debug_log("tp worker %lu-%lu stopped\n", tp->id, thread_id);
	amsync_dec(&tp->idle_thread_count);
	amsync_dec(&tp->active_thread_count);
	amsync_inc(&tp->threads_destroyed);
	return NULL;
}

static amrc_t lam_thread_pool_start_thread(lam_thread_pool_t *tp)
{
	int rc;
	pthread_t th;
	uint64_t active;

	if (tp->drain_signal)
		return AMRC_ERROR;

	active = amsync_inc(&tp->threads_created) - tp->threads_destroyed;
	if ((tp->config.max_threads > 0) && (active >= tp->config.max_threads)) {
		amsync_dec(&tp->threads_created);
		return AMRC_SUCCESS;
	}

	rc = pthread_create(&th, NULL, lam_thread_pool_worker_func, tp);
	if (rc != 0)
		return AMRC_ERROR;

	rc = pthread_detach(th);
	if (rc != 0) {
		/* Electing to not implement recovery here */
		abort();
	}

	return AMRC_SUCCESS;
}

/* Allocates a thread pool
 * NOTE: Is not thread safe with other lam_thread_pool_create/lam_thread_pool_destroy.
 *
 * @Returns pointer to pool handle / NULL on error */
lam_thread_pool_t* lam_thread_pool_create(const lam_thread_pool_config_t *config)
{
	struct timespec poll_timeout;
	lam_thread_pool_t *tp;
	uint64_t i;
	amrc_t rc;

	tp = malloc(sizeof(*tp));
	if (tp == NULL)
		goto ret_error;
	memset(tp, 0, sizeof(*tp));

	if (config != NULL) {
		memcpy(&tp->config, config, sizeof(tp->config));
	}
	if (tp->config.poll_freq == 0)
		tp->config.poll_freq = 5 * AMTIME_MSEC;
	if (tp->config.backlog == 0)
		tp->config.backlog = 15;
	if (tp->config.min_threads == 0)
		tp->config.min_threads = 1;
	if (tp->config.max_threads && tp->config.max_threads < tp->config.min_threads)
		tp->config.max_threads = tp->config.min_threads;

	tp->tasks_queue = amstack_alloc(tp->config.backlog);
	if (tp->tasks_queue == NULL)
		goto free_tp;

	rc = pthread_mutex_init(&tp->stats_mutex, NULL);
	if (rc != 0)
		goto free_queue;

	tp->id = amsync_inc(&thread_pool_index);
	tp->running_id = 1;
	lam_thread_pool_stats_init(&tp->stats);

	if (!(tp->config.flags & LIBAM_THREAD_POOL_LAZY_START)) {
		for (i = 0; i < tp->config.min_threads; i++) {
			rc = lam_thread_pool_start_thread(tp);
			if (rc != AMRC_SUCCESS)
				goto drain;
		}
	}

	debug_log("tp %lu created\n", tp->id);
	return tp;

drain:
	tp->drain_signal = 1;
	while (tp->active_thread_count > 0) {
		poll_timeout.tv_sec = 0;
		poll_timeout.tv_nsec = tp->config.poll_freq;
		nanosleep(&poll_timeout, NULL);
	}
	pthread_mutex_destroy(&tp->stats_mutex);
free_queue:
	amstack_free(tp->tasks_queue);
free_tp:
	free(tp);
ret_error:
	return NULL;
}

amrc_t lam_thread_pool_destroy(lam_thread_pool_t *tp, lam_thread_pool_stats_t *stats)
{
	struct timespec poll_timeout;

	if (tp == NULL)
		return AMRC_ERROR;

	debug_log("tp %lu draining\n", tp->id);
	tp->drain_signal = 1;
	while (tp->threads_destroyed < tp->threads_created) {
		poll_timeout.tv_sec = 0;
		poll_timeout.tv_nsec = tp->config.poll_freq;
		nanosleep(&poll_timeout, NULL);
	}
	pthread_mutex_destroy(&tp->stats_mutex);
	amstack_free(tp->tasks_queue);

	if (stats != NULL) {
		*stats = tp->stats;
		stats->threads_created = tp->threads_created;
		stats->tasks_created = tp->tasks_created;
	}

	debug_log("tp %lu destroyed\n", tp->id);
	free(tp);
	return AMRC_SUCCESS;
}

uint64_t lam_thread_pool_get_thread_count(const lam_thread_pool_t *tp)
{
	return tp->active_thread_count;
}

uint64_t lam_thread_pool_get_idle_thread_count(const lam_thread_pool_t *tp)
{
	return tp->idle_thread_count;
}

uint64_t lam_thread_pool_get_min_thread_count(const lam_thread_pool_t *tp)
{
	return tp->config.min_threads;
}

uint64_t lam_thread_pool_get_max_thread_count(const lam_thread_pool_t *tp)
{
	return tp->config.max_threads;
}

amrc_t lam_thread_pool_set_default_func(lam_thread_pool_t *tp, lam_thread_func_t value)
{
	if (tp == NULL || value == NULL)
		return AMRC_ERROR;
	tp->config.default_func = value;
	return AMRC_SUCCESS;
}

amrc_t lam_thread_pool_set_idle_timeout(lam_thread_pool_t *tp, amtime_t value)
{
	if (tp == NULL)
		return AMRC_ERROR;
	tp->config.idle_timeout = value;
	return AMRC_SUCCESS;
}

amrc_t lam_thread_pool_set_min_thread_count(lam_thread_pool_t *tp, uint64_t value)
{
	amrc_t ret = AMRC_SUCCESS;
	amrc_t rc;

	if (tp == NULL || value == 0)
		return AMRC_ERROR;
	tp->config.min_threads = value;

	if (!(tp->config.flags & LIBAM_THREAD_POOL_LAZY_START)) {
		uint64_t active = tp->active_thread_count;
		if (active < value)
			for (; active < value; active++) {
				rc = lam_thread_pool_start_thread(tp);
				if (rc != AMRC_SUCCESS)
					ret = AMRC_ERROR;
			}
	}

	return ret;
}

amrc_t lam_thread_pool_set_max_thread_count(lam_thread_pool_t *tp, uint64_t value)
{
	if (tp == NULL)
		return AMRC_ERROR;
	tp->config.max_threads = value;
	return AMRC_SUCCESS;
}

/* Queue a task to execute via the thread pool
 * @Returns AMRC_SUCCESS / AMRC_ERROR. Nothing is queued when errors happen */
amrc_t lam_thread_pool_run(lam_thread_pool_t* tp, lam_thread_func_t func, void* arg, void** ret_ptr)
{
	lam_thread_pool_task_t *task;
	amrc_t rc;

	if (tp == NULL || tp->drain_signal)
		{abort(); return AMRC_ERROR;}

	if (func == NULL) {
		if (tp->config.default_func == NULL)
			return AMRC_ERROR;
		func = tp->config.default_func;
	}
	else if (tp->config.default_func != NULL && !(tp->config.flags & LIBAM_THREAD_POOL_FUNC_OVERRIDE)){
		/* Only allow custom <func> to override <tp->config.default_func> when LIBAM_THREAD_POOL_FUNC_OVERRIDE set */
		return AMRC_ERROR;
	}

	task = malloc(sizeof(*task));
	if (task == NULL)
		return AMRC_ERROR;
	task->id = amsync_inc(&tp->tasks_created) + 1;
	task->func = func;
	task->arg = arg;
	task->ret_ptr = ret_ptr;

	/* Accounting */
	task->queue_time = amtime_now();
	task->queue_depth = amstack_get_size(tp->tasks_queue);
	task->active_thread_count = tp->active_thread_count;
	task->idle_thread_count = tp->idle_thread_count;
	if (task->idle_thread_count > task->active_thread_count) {
		/* Can happen due to thread scheduling */
		task->active_thread_count = task->idle_thread_count;
	}

	/* Figure out if we need to start thread */
	if (task->idle_thread_count == 0) {
		rc = lam_thread_pool_start_thread(tp);
		if (rc != AMRC_SUCCESS) {
			free(task);
			return AMRC_ERROR;
		}
	}

	/* Queue task */
	rc = amstack_push(tp->tasks_queue, task);
	if (rc != AMRC_SUCCESS)
		return AMRC_ERROR;

	debug_log("tp %lu enqueued task %lu-%lu (%p)\n", tp->id, tp->id, task->id, arg);
	return AMRC_SUCCESS;
}
