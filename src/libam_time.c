#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "libam/libam_time.h"

typedef struct amtime_thread {
	pthread_t thread;
	volatile amtime_t refresh_period;
	volatile amtime_t max_drift;
	volatile amtime_t now;
	volatile uint8_t stop;
	volatile uint8_t thread_free;
	volatile uint8_t ignore_drift;
} amtime_thread_t;

static void* amtime_preiodic_thread_func(void* data)
{
	amtime_thread_t* handle = data;
	amtime_t new_now;
	amtime_t old_now;
	amtime_t ref_period;

	while (!handle->stop) {
		new_now = amtime_now();
		ref_period = handle->refresh_period;

		if (handle->ignore_drift) {
			handle->max_drift = 1;
			handle->ignore_drift = 0;
		}
		else {
			old_now = handle->now;
			if ((new_now - old_now > ref_period) && (new_now - old_now > handle->max_drift))
				handle->max_drift = new_now - old_now;
		}

		handle->now = amtime_now();
		usleep(ref_period);
	}

	if (handle->thread_free)
		free(handle);

	return NULL;
}

/* Starts a thread to update time periodically with specified interval.
 * Interval specifies minimum suspend time, in microseconds. Zero defaults to AMTIME_DEFAULT_REFRESH_PERIOD.
 * Returns Thread handle / NULL on error */
struct amtime_thread* amtime_preiodic_start(amtime_t refresh_period)
{
	amtime_thread_t* handle;
	int rc;

	handle = malloc(sizeof(*handle));
	if (handle == NULL)
		return NULL;

	memset(handle, 0, sizeof(*handle));
	handle->now = amtime_now();
	handle->max_drift = 1;
	handle->stop = 0;
	handle->thread_free = 0;
	handle->ignore_drift = 0;
	handle->refresh_period = (refresh_period > 0 ? refresh_period : AMTIME_DEFAULT_REFRESH_PERIOD);

	rc = pthread_create(&handle->thread, NULL, amtime_preiodic_thread_func, handle);
	if (rc != 0) {
		free(handle);
		return NULL;
	}

	return handle;
}

/* Stops the thread and invalidates the handle
 * Blocks unless should_block is set to 0. In non-blocking mode there's no way to verify success of thread stoppage.
 * Returns Max drift of thread on success / 0 on error */
amtime_t amtime_preiodic_stop(struct amtime_thread* handle, uint8_t should_block)
{
	int rc;
	amtime_t max_drift;

	if (handle == NULL)
		return 0;

	if (!should_block) {
		handle->thread_free = 1;
		handle->stop = 1;
		return (handle->max_drift > 0 ? handle->max_drift : 1);
	}

	/* Blocking operation */
	handle->stop = 1;
	rc = pthread_join(handle->thread, NULL);
	if (rc != 0) {
		return 0;
	}

	max_drift = (handle->max_drift > 0 ? handle->max_drift : 1);
	free(handle);

	return max_drift;
}

/* Retreives the last updated periodic time / 0 on invalid handle */
amtime_t amtime_preiodic_now(struct amtime_thread* handle)
{
	return (handle ? handle->now : 0);
}


/* Updates minimum suspend time, in microseconds. Zero defaults to AMTIME_DEFAULT_REFRESH_PERIOD. */
void amtime_preiodic_set_refresh_period(struct amtime_thread* handle, amtime_t refresh_period)
{
	if (handle == NULL)
		return;
	handle->ignore_drift = 1;
	handle->refresh_period = (refresh_period > 0 ? refresh_period : AMTIME_DEFAULT_REFRESH_PERIOD);
}

/* Returns current refresh period / 0 on invalid handle */
amtime_t amtime_preiodic_get_refresh_period(struct amtime_thread* handle)
{
	return (handle ? handle->refresh_period : 0);
}

/* Returns the largest difference between configured refresh period and actual refresh times since start or last reset.
 * Returns max_drift_time in microseconds / 0 on invalid handle */
amtime_t amtime_periodic_get_max_drift(struct amtime_thread* handle)
{
	return (handle ? handle->max_drift : 0);
}

/* Resets max drift counter */
void amtime_periodic_del_max_drift(struct amtime_thread* handle)
{
	if (handle == NULL)
		return;
	handle->max_drift = 1;
}

