#ifndef _LIBAM_TIME_H_
#define _LIBAM_TIME_H_

#include <sys/time.h>
#include <time.h>
#include <stdint.h>
#include <stddef.h>

typedef uint64_t amtime_t;

#define AMTIME_MAX	(-1UL)
#define AMTIME_USEC	((amtime_t)1)
#define AMTIME_MSEC	(AMTIME_USEC * 1000)
#define AMTIME_SEC	(AMTIME_MSEC * 1000)
#define AMTIME_MIN	(AMTIME_SEC * 60)

// Returns time since epoch in microseconds
static inline amtime_t amtime_now()
{
	struct timeval amtimeval;
	if (gettimeofday(&amtimeval, NULL) == -1) {
		return 0;
	}
	return (((uint64_t)amtimeval.tv_sec) * AMTIME_SEC) + amtimeval.tv_usec;
}


struct amtime_thread;

#define AMTIME_DEFAULT_REFRESH_PERIOD (AMTIME_MSEC)

/* Starts a thread to update time periodically with specified interval.
 * Interval specifies minimum suspend time, in microseconds. Zero defaults to AMTIME_DEFAULT_REFRESH_PERIOD.
 * Returns Thread handle / NULL on error */
struct amtime_thread* amtime_preiodic_start(amtime_t refresh_period);

/* Stops the thread and invalidates the handle
 * Blocks unless should_block is set to 0. In non-blocking mode there's no way to verify success of thread stoppage.
 * Returns Max drift of thread on success / 0 on error */
amtime_t amtime_preiodic_stop(struct amtime_thread* handle, uint8_t should_block);

/* Retreives the last updated periodic time / 0 on invalid handle */
amtime_t amtime_preiodic_now(struct amtime_thread* handle);


/* Updates minimum suspend time, in microseconds. Zero defaults to AMTIME_DEFAULT_REFRESH_PERIOD. */
void amtime_preiodic_set_refresh_period(struct amtime_thread* handle, amtime_t refresh_period);

/* Returns current refresh period / 0 on invalid handle */
amtime_t amtime_preiodic_get_refresh_period(struct amtime_thread* handle);

/* Returns the largest difference between configured refresh period and actual refresh times since start or last reset.
 * Returns max_drift_time in microseconds / 0 on invalid handle */
amtime_t amtime_periodic_get_max_drift(struct amtime_thread* handle);

/* Resets max drift counter */
void amtime_periodic_del_max_drift(struct amtime_thread* handle);


#endif
